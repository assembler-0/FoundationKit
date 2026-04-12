#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/Cas.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // TaggedPtr<T>
    //
    // ## Purpose
    //
    // A 128-bit {T*, u64 generation} pair updated by a single atomic CAS.
    // The generation counter is incremented on every successful swap, making
    // the ABA problem detectable: a pointer removed and re-inserted between
    // two CAS attempts will have a different generation even if the address
    // is identical.
    //
    // ## Architecture dispatch
    //
    // All 128-bit CAS logic lives in Sync/Cas.hpp. TaggedPtr does not contain
    // a single #if arch — it calls Sync::Cas128 on native-CAS architectures
    // and falls back to an embedded SpinLock on RISC-V (where Sync::Cas128
    // as a free function is intentionally unavailable).
    //
    // ## Layout (alignas(16))
    //
    //   [0..7]   m_ptr  — pointer value stored as u64
    //   [8..15]  m_gen  — generation counter (wraps at 2^64)
    //   [16+]    m_lock — SpinLock, [[no_unique_address]] on native-CAS arches
    //                     (zero overhead on x86-64 / AArch64)
    //
    // ## Limitations
    //
    // - On RISC-V, CompareExchange acquires an embedded SpinLock and is
    //   therefore not obstruction-free (though it is still deadlock-free).
    // - The generation counter wraps at 2^64. Practical ABA via wrap-around
    //   requires 2^64 swaps on the same slot — not a real concern.
    // =========================================================================

    /// @brief ABA-safe tagged pointer: {T*, u64 generation}.
    ///
    /// @tparam T  Pointee type. Must not be void or a reference.
    template<typename T>
    class TaggedPtr {
        static_assert(!Void<T>, "TaggedPtr: T must not be void");
        static_assert(!Reference<T>, "TaggedPtr: T must not be a reference");

    public:
        /// @brief The value type returned by Load() and consumed by CompareExchange().
        struct Pair {
            T *ptr;
            u64 gen;
        };

        /// @brief Construct with an initial pointer and generation zero.
        explicit constexpr TaggedPtr(T *initial = nullptr) noexcept : m_ptr(reinterpret_cast<u64>(initial)), m_gen(0) {}

        TaggedPtr(const TaggedPtr &) = delete;
        TaggedPtr &operator=(const TaggedPtr &) = delete;

        /// @brief Atomically load the current {ptr, gen} pair.
        ///
        /// On native-CAS architectures (x86-64, AArch64) this is a single
        /// 128-bit atomic load implemented as a no-op CAS (expected == desired,
        /// so the CAS always succeeds without modifying the value).
        /// On RISC-V the embedded SpinLock serialises the two 64-bit loads.
        [[nodiscard]] Pair Load() const noexcept {
            if constexpr (kHasNative128BitCas) {
                u64 lo = m_ptr;
                u64 hi = m_gen;
                // No-op CAS: expected == desired, so the instruction reads
                // the current 128-bit value into lo/hi atomically. The return
                // value is intentionally discarded — on "failure" (which is
                // the only possible outcome when desired == expected and another
                // CPU may have written) lo/hi are updated with the actual value.
                (void)Cas128(const_cast<volatile u64 *>(&m_ptr), lo, hi, lo, hi);
                return {reinterpret_cast<T *>(lo), hi};
            } else {
                LockGuard guard(m_lock);
                return {reinterpret_cast<T *>(m_ptr), m_gen};
            }
        }

        /// @brief Atomically compare-and-swap the {ptr, gen} pair.
        ///
        /// If the current value equals `expected`, atomically writes `desired`
        /// and returns true. On failure, `expected` is updated with the current
        /// value so the caller can retry without an extra Load().
        ///
        /// @param expected  In: value to compare. Out: current value on failure.
        /// @param desired   Value to write on success.
        /// @return          True if the swap succeeded.
        [[nodiscard]] bool CompareExchange(Pair &expected, Pair desired) noexcept {
            FK_BUG_ON(desired.gen == expected.gen && desired.ptr == expected.ptr,
                      "TaggedPtr::CompareExchange: desired == expected — CAS cannot make "
                      "progress (increment the generation counter on every swap)");

            if constexpr (kHasNative128BitCas) {
                u64 exp_lo = reinterpret_cast<u64>(expected.ptr);
                u64 exp_hi = expected.gen;
                const bool ok = Cas128(&m_ptr, exp_lo, exp_hi, reinterpret_cast<u64>(desired.ptr), desired.gen);
                if (!ok) {
                    expected.ptr = reinterpret_cast<T *>(exp_lo);
                    expected.gen = exp_hi;
                }
                return ok;
            } else {
                LockGuard guard(m_lock);
                if (m_ptr == reinterpret_cast<u64>(expected.ptr) && m_gen == expected.gen) {
                    m_ptr = reinterpret_cast<u64>(desired.ptr);
                    m_gen = desired.gen;
                    return true;
                }
                expected.ptr = reinterpret_cast<T *>(m_ptr);
                expected.gen = m_gen;
                return false;
            }
        }

        /// @brief Unconditional store — NOT atomic.
        ///
        /// Use only during single-threaded initialisation, before this object
        /// is published to other CPUs. After publication, use CompareExchange.
        void StoreRelaxed(T *ptr, u64 gen = 0) noexcept {
            m_ptr = reinterpret_cast<u64>(ptr);
            m_gen = gen;
        }

    private:
        alignas(16) volatile u64 m_ptr;
        volatile u64 m_gen;

        [[no_unique_address]] mutable SpinLock m_lock;
    };

    // =========================================================================
    // TaggedPtrLike concept — for generic lock-free data structures.
    // =========================================================================

    template<typename P>
    concept TaggedPtrLike = requires(P &p, typename P::Pair pair) {
        { p.Load() } -> SameAs<typename P::Pair>;
        { p.CompareExchange(pair, pair) } -> SameAs<bool>;
    };

    static_assert(TaggedPtrLike<TaggedPtr<int>>);

} // namespace FoundationKitCxxStl::Sync
