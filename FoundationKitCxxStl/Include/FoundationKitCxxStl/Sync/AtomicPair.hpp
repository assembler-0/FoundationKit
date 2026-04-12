#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/Cas.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // AtomicPair<T, U>
    //
    // ## Why this exists
    //
    // Atomic<T> has static_assert(sizeof(T) <= 8). This is correct: the
    // __atomic GCC/Clang builtins only guarantee lock-free operation up to
    // the pointer width (8 bytes on 64-bit). For 16-byte values the compiler
    // silently falls back to a libatomic call, which is a hosted-runtime
    // dependency that does not exist in a freestanding kernel.
    //
    // AtomicPair<T, U> fills this gap. It stores a {T, U} pair as two
    // adjacent u64 words and uses Sync::Cas128 for all atomic operations.
    // The architecture dispatch (CMPXCHG16B / LDAXP+STLXP / SpinLock) is
    // fully encapsulated in Cas.hpp — AtomicPair contains zero #if arch.
    //
    // ## Constraints
    //
    //   sizeof(T) <= 8 && sizeof(U) <= 8   — each half fits in a u64.
    //   TriviallyCopyable<T> && TriviallyCopyable<U> — BitCast is safe.
    //   alignas(16)                         — required by CMPXCHG16B / LDAXP.
    //
    // ## Memory ordering
    //
    // All operations use AcqRel semantics on the CAS path (matching the
    // contract of Sync::Cas128). Load() is implemented as a no-op CAS
    // (expected == desired) so it also carries Acquire semantics.
    // Store() is implemented as a CAS retry loop with Release on success.
    //
    // ## Relationship to TaggedPtr
    //
    // TaggedPtr<T> is a specialised AtomicPair<T*, u64> with an added
    // generation-counter invariant. It is kept as a separate type because
    // its API (Pair, ABA semantics, FK_BUG_ON on same-value CAS) is
    // domain-specific. AtomicPair is the general-purpose primitive.
    //
    // ## RISC-V fallback
    //
    // On RISC-V (kHasNative128BitCas == false) all operations acquire an
    // embedded SpinLock. The lock is [[no_unique_address]] on native-CAS
    // architectures so it costs nothing there.
    // =========================================================================

    /// @brief A pair of values {T, U} updated atomically as a 128-bit unit.
    ///
    /// @tparam T  First field type. sizeof(T) must be <= 8. Must be TriviallyCopyable.
    /// @tparam U  Second field type. sizeof(U) must be <= 8. Must be TriviallyCopyable.
    template <typename T, typename U>
    class AtomicPair {
        static_assert(TriviallyCopyable<T>,
            "AtomicPair: T must be TriviallyCopyable");
        static_assert(TriviallyCopyable<U>,
            "AtomicPair: U must be TriviallyCopyable");
        static_assert(sizeof(T) <= 8,
            "AtomicPair: sizeof(T) must be <= 8 (fits in one u64 half)");
        static_assert(sizeof(U) <= 8,
            "AtomicPair: sizeof(U) must be <= 8 (fits in one u64 half)");

    public:
        /// @brief The value type returned by Load() and consumed by CompareExchange().
        struct Pair {
            T first;
            U second;
        };

        /// @brief Construct with initial values.
        explicit constexpr AtomicPair(T first = T{}, U second = U{}) noexcept {
            // Plain writes during construction — the object is not yet
            // published to other CPUs so no atomic operation is needed.
            m_lo = ToU64(first);
            m_hi = ToU64(second);
        }

        AtomicPair(const AtomicPair&)            = delete;
        AtomicPair& operator=(const AtomicPair&) = delete;

        /// @brief Atomically load the current {first, second} pair.
        ///
        /// Implemented as a no-op CAS (expected == desired). On native-CAS
        /// architectures this is a single 128-bit atomic read. On RISC-V
        /// the embedded SpinLock serialises the two 64-bit loads.
        [[nodiscard]] Pair Load() const noexcept {
            if constexpr (kHasNative128BitCas) {
                u64 lo = m_lo;
                u64 hi = m_hi;
                (void)Cas128(const_cast<volatile u64*>(&m_lo), lo, hi, lo, hi);
                return {FromU64<T>(lo), FromU64<U>(hi)};
            } else {
                LockGuard guard(m_lock);
                return {FromU64<T>(m_lo), FromU64<U>(m_hi)};
            }
        }

        /// @brief Atomically store a new {first, second} pair.
        ///
        /// Implemented as a CAS retry loop. On native-CAS architectures
        /// this is wait-free in practice (the loop retries only on
        /// concurrent modification). On RISC-V it acquires the SpinLock.
        void Store(T first, U second) noexcept {
            if constexpr (kHasNative128BitCas) {
                u64 exp_lo = m_lo;
                u64 exp_hi = m_hi;
                const u64 des_lo = ToU64(first);
                const u64 des_hi = ToU64(second);
                while (!Cas128(&m_lo, exp_lo, exp_hi, des_lo, des_hi)) {
                    CompilerBuiltins::CpuPause();
                }
            } else {
                LockGuard guard(m_lock);
                m_lo = ToU64(first);
                m_hi = ToU64(second);
            }
        }

        /// @brief Atomically compare-and-swap the pair.
        ///
        /// If the current value equals `expected`, atomically writes `desired`
        /// and returns true. On failure, `expected` is updated with the current
        /// value so the caller can retry without an extra Load().
        ///
        /// @param expected  In: value to compare. Out: current value on failure.
        /// @param desired   Value to write on success.
        /// @return          True if the swap succeeded.
        [[nodiscard]] bool CompareExchange(Pair& expected, const Pair& desired) noexcept {
            if constexpr (kHasNative128BitCas) {
                u64 exp_lo = ToU64(expected.first);
                u64 exp_hi = ToU64(expected.second);
                const bool ok = Cas128(
                    &m_lo,
                    exp_lo, exp_hi,
                    ToU64(desired.first), ToU64(desired.second));
                if (!ok) {
                    expected.first  = FromU64<T>(exp_lo);
                    expected.second = FromU64<U>(exp_hi);
                }
                return ok;
            } else {
                LockGuard guard(m_lock);
                const u64 cur_lo = m_lo;
                const u64 cur_hi = m_hi;
                if (cur_lo == ToU64(expected.first) &&
                    cur_hi == ToU64(expected.second)) {
                    m_lo = ToU64(desired.first);
                    m_hi = ToU64(desired.second);
                    return true;
                }
                expected.first  = FromU64<T>(cur_lo);
                expected.second = FromU64<U>(cur_hi);
                return false;
            }
        }

    private:
        template <typename V>
        static u64 ToU64(V val) noexcept {
            u64 result = 0;
            CompilerBuiltins::MemCpy(&result, &val, sizeof(V));
            return result;
        }

        template <typename V>
        static V FromU64(u64 raw) noexcept {
            V result{};
            CompilerBuiltins::MemCpy(&result, &raw, sizeof(V));
            return result;
        }

        alignas(16) volatile u64 m_lo;
                    volatile u64 m_hi;

        [[no_unique_address]] mutable SpinLock m_lock;
    };

    // =========================================================================
    // Concept: AtomicPairLike
    // =========================================================================

    template <typename P>
    concept AtomicPairLike = requires(P& p, typename P::Pair pair) {
        { p.Load() }                          -> SameAs<typename P::Pair>;
        { p.Store(pair.first, pair.second) };
        { p.CompareExchange(pair, pair) }     -> SameAs<bool>;
    };

    static_assert(AtomicPairLike<AtomicPair<u32, u64>>);
    static_assert(AtomicPairLike<AtomicPair<u64, u64>>);

} // namespace FoundationKitCxxStl::Sync
