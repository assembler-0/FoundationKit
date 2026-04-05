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

        /// @brief Operator overloads for convenience.
        operator T() const noexcept { return Load(); }
        T operator=(T value) noexcept { Store(value); return value; }

        T operator++() noexcept { return FetchAdd(1) + 1; }
        T operator++(int) noexcept { return FetchAdd(1); }
        T operator--() noexcept { return FetchSub(1) - 1; }
        T operator--(int) noexcept { return FetchSub(1); }

        T operator+=(T val) noexcept { return FetchAdd(val) + val; }
        T operator-=(T val) noexcept { return FetchSub(val) - val; }

    private:
        mutable T m_value;
    };

} // namespace FoundationKitCxxStl::Sync
