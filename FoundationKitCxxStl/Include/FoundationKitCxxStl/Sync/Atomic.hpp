#pragma once

#include <FoundationKitCxxStl/Base/Safety.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl::Base;

    /// @brief Memory ordering constraints for atomic operations.
    enum class MemoryOrder : int {
        Relaxed = __ATOMIC_RELAXED,
        Consume = __ATOMIC_CONSUME,
        Acquire = __ATOMIC_ACQUIRE,
        Release = __ATOMIC_RELEASE,
        AcqRel  = __ATOMIC_ACQ_REL,
        SeqCst  = __ATOMIC_SEQ_CST
    };

    /// @brief Atomic wrapper for thread-safe access to a value.
    /// @tparam T Must be a trivially copyable type (ideally integral or pointer).
    template <typename T>
    class Atomic {
        static_assert(TriviallyCopyable<T>,
            "Atomic<T>: T must be trivially copyable (required by all atomic hardware implementations)");
        static_assert(sizeof(T) <= 8,
            "Atomic<T>: T must be at most 8 bytes (64-bit atomic operations are the widest guaranteed lock-free)");
        using _check = TypeSanityCheck<T>;
    public:
        constexpr Atomic() noexcept : m_value{} {}
        constexpr Atomic(T value) noexcept : m_value(value) {}

        // Non-copyable, non-movable
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        /// @brief Load the value atomically.
        [[nodiscard]] T Load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept {
            return CompilerBuiltins::AtomicLoad(&m_value, static_cast<int>(order));
        }

        /// @brief Store a value atomically.
        void Store(T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
            CompilerBuiltins::AtomicStore(&m_value, value, static_cast<int>(order));
        }

        /// @brief Atomic exchange (read-modify-write).
        T Exchange(T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
            return CompilerBuiltins::AtomicExchange(&m_value, value, static_cast<int>(order));
        }

        /// @brief Atomic compare and exchange.
        bool CompareExchange(T& expected, T desired, bool weak = false, 
                            MemoryOrder success = MemoryOrder::SeqCst, 
                            MemoryOrder failure = MemoryOrder::SeqCst) noexcept {
            return CompilerBuiltins::AtomicCompareExchange(&m_value, &expected, desired, weak, 
                                                           static_cast<int>(success), 
                                                           static_cast<int>(failure));
        }

        /// @brief Arithmetic operations (only for integral/pointer types).
        T FetchAdd(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires (Integral<T> || Pointer<T>) {
            return CompilerBuiltins::AtomicFetchAdd(&m_value, val, static_cast<int>(order));
        }

        T FetchSub(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires (Integral<T> || Pointer<T>) {
            return CompilerBuiltins::AtomicFetchSub(&m_value, val, static_cast<int>(order));
        }

        /// @brief Bitwise operations.
        T FetchAnd(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires Integral<T> {
            return CompilerBuiltins::AtomicFetchAnd(&m_value, val, static_cast<int>(order));
        }

        T FetchOr(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires Integral<T> {
            return CompilerBuiltins::AtomicFetchOr(&m_value, val, static_cast<int>(order));
        }

        T FetchXor(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires Integral<T> {
            return CompilerBuiltins::AtomicFetchXor(&m_value, val, static_cast<int>(order));
        }

        /// @brief FetchMax: atomically store max(current, val), return old value.
        /// @desc  Uses a weak CAS loop. On x86 the loop almost never retries;
        ///        on ARM the LL/SC pair may retry under contention.
        T FetchMax(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires (Integral<T> || Pointer<T>) {
            T current = Load(MemoryOrder::Relaxed);
            while (current < val) {
                // Weak CAS: failure refreshes `current` automatically.
                if (CompareExchange(current, val, true, order, MemoryOrder::Relaxed))
                    return current;
            }
            return current;
        }

        /// @brief FetchMin: atomically store min(current, val), return old value.
        T FetchMin(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept requires (Integral<T> || Pointer<T>) {
            T current = Load(MemoryOrder::Relaxed);
            while (current > val) {
                if (CompareExchange(current, val, true, order, MemoryOrder::Relaxed))
                    return current;
            }
            return current;
        }

        /// @brief Implicit load for read contexts (Acquire).
        operator T() const noexcept { return Load(); }

        /// @brief Assignment operator deleted: `atomic = x` looks like a plain
        ///        store but emits a full SeqCst fence. Use Store(v, order) explicitly
        ///        to make the intended ordering visible at the call site.
        T operator=(T) = delete;

        T operator++() noexcept { return FetchAdd(1) + 1; }
        T operator++(int) noexcept { return FetchAdd(1); }
        T operator--() noexcept { return FetchSub(1) - 1; }
        T operator--(int) noexcept { return FetchSub(1); }

        T operator+=(T val) noexcept { return FetchAdd(val) + val; }
        T operator-=(T val) noexcept { return FetchSub(val) - val; }

    private:
        mutable T m_value;
    };

    /// @brief Establishes memory synchronization ordering of non-atomic and relaxed atomic accesses.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void AtomicThreadFence(MemoryOrder order) noexcept {
        CompilerBuiltins::AtomicThreadFence(static_cast<int>(order));
    }

} // namespace FoundationKitCxxStl::Sync
