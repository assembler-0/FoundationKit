#pragma once

// ============================================================================
// Compiler Feature Detection
// ============================================================================

#if defined(__has_builtin)
    #define FOUNDATIONKIT_COMPILER_HAS_BUILTIN(b) __has_builtin(b)
#else
    #define FOUNDATIONKIT_COMPILER_HAS_BUILTIN(b) 0
#endif

#if defined(__has_attribute)
    #define FOUNDATIONKIT_COMPILER_HAS_ATTRIBUTE_ALIAS(a) __has_attribute(a)
#endif

namespace FoundationKitCxxStl::Base::CompilerBuiltins {

    /// @breif Hints to the compiler that 'exp' is likely to have the value 'c'.
    template <typename T>
    constexpr T Expect(T exp, T c) noexcept {
        return __builtin_expect(exp, c);
    }

    /// @breif Informs the compiler that this point in the code is unreachable.
    [[noreturn]] inline void Unreachable() noexcept {
        __builtin_unreachable();
    }

    /// @breif Performs a bitwise cast from 'From' to 'To'.
    template <typename To, typename From>
    constexpr To BitCast(const From& from) noexcept {
        return __builtin_bit_cast(To, from);
    }

    /// @breif Counts leading zero bits.
    constexpr int CountLeadingZeros(unsigned int value) noexcept {
        return __builtin_clz(value);
    }

    constexpr int CountLeadingZerosL(unsigned long value) noexcept {
        return __builtin_clzl(value);
    }

    constexpr int CountLeadingZerosLL(unsigned long long value) noexcept {
        return __builtin_clzll(value);
    }

    /// @breif Counts trailing zero bits.
    constexpr int CountTrailingZeros(unsigned int value) noexcept {
        return __builtin_ctz(value);
    }

    constexpr int CountTrailingZerosL(unsigned long value) noexcept {
        return __builtin_ctzl(value);
    }

    constexpr int CountTrailingZerosLL(unsigned long long value) noexcept {
        return __builtin_ctzll(value);
    }

    /// @breif Counts the number of set bits (population count).
    constexpr int PopCount(unsigned int value) noexcept {
        return __builtin_popcount(value);
    }

    constexpr int PopCountL(unsigned long value) noexcept {
        return __builtin_popcountl(value);
    }

    constexpr int PopCountLL(unsigned long long value) noexcept {
        return __builtin_popcountll(value);
    }

    /// @brief Population count dispatched on the native word size (usize).
    ///        On LP64 targets usize == unsigned long; on ILP32 it is unsigned int.
    ///        This avoids callers having to pick the right suffix manually.
    constexpr int PopCountUSize(unsigned long value) noexcept {
        if constexpr (sizeof(unsigned long) == sizeof(unsigned int)) {
            return __builtin_popcount(static_cast<unsigned int>(value));
        } else {
            return __builtin_popcountl(value);
        }
    }

    /// @brief Count trailing zeros dispatched on the native word size (usize).
    constexpr int CountTrailingZerosUSize(unsigned long value) noexcept {
        if constexpr (sizeof(unsigned long) == sizeof(unsigned int)) {
            return __builtin_ctz(static_cast<unsigned int>(value));
        } else {
            return __builtin_ctzl(value);
        }
    }

    /// @breif Memory operations.
    inline void MemCpy(void* dest, const void* src, decltype(sizeof(0)) size) noexcept {
        __builtin_memcpy(dest, src, size);
    }

    inline void MemMove(void* dest, const void* src, decltype(sizeof(0)) size) noexcept {
        __builtin_memmove(dest, src, size);
    }

    inline void MemSet(void* dest, int value, decltype(sizeof(0)) size) noexcept {
        __builtin_memset(dest, value, size);
    }

    inline int MemCmp(const void* lhs, const void* rhs, decltype(sizeof(0)) size) noexcept {
        return __builtin_memcmp(lhs, rhs, size);
    }

    /// @breif Overflow-safe arithmetic.
    template <typename T>
    bool AddOverflow(T a, T b, T* res) noexcept {
        return __builtin_add_overflow(a, b, res);
    }

    template <typename T>
    bool SubOverflow(T a, T b, T* res) noexcept {
        return __builtin_sub_overflow(a, b, res);
    }

    template <typename T>
    bool MulOverflow(T a, T b, T* res) noexcept {
        return __builtin_mul_overflow(a, b, res);
    }

    /// @breif Byte swapping.
    constexpr unsigned short ByteSwap16(unsigned short value) noexcept {
        return __builtin_bswap16(value);
    }

    constexpr unsigned int ByteSwap32(unsigned int value) noexcept {
        return __builtin_bswap32(value);
    }

    constexpr unsigned long long ByteSwap64(unsigned long long value) noexcept {
        return __builtin_bswap64(value);
    }

    /// @breif Checks if 'value' is a compile-time constant.
    template <typename T>
    constexpr bool IsConstant(T value) noexcept {
        return __builtin_constant_p(value);
    }

    /// @breif Prefetch memory into cache.
    #define FOUNDATIONKITCXXSTL_BUILTIN_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)

    /// @breif Offset of member
    #define FOUNDATIONKITCXXSTL_BUILTIN_OFFSET_OF(type, member) __builtin_offsetof(type, member)

    /// @breif Triggers a debugger trap or aborts execution.
    inline void Trap() noexcept {
        __builtin_trap();
    }

    /// @breif Returns the return address of the current function.
    /// @note Level MUST be a constant.
    inline void* ReturnAddress() noexcept {
        return __builtin_return_address(0);
    }

    /// @breif Returns the frame address of the current function.
    /// @note Level MUST be a constant.
    inline void* FrameAddress() noexcept {
        return __builtin_frame_address(0);
    }

    /// @breif Informs the compiler to assume 'condition' is true.
    inline void Assume(bool condition) noexcept {
        if (!condition) Unreachable();
    }

    /// @brief Prevents the compiler from optimizing away 'value'.
    template <typename T>
    inline void DoNotOptimize(T& value) noexcept {
#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
        __asm__ volatile("" : : "g"(value) : "memory");
#else
        static_cast<void>(value);
#endif
    }

    // ========================================================================
    // CPU Control Instructions
    // ========================================================================

    /// @brief Issue a CPU pause instruction (PAUSE on x86, YIELD on ARM).
    /// @desc Reduces power consumption in spinloop scenarios.
    inline void CpuPause() noexcept {
        __builtin_ia32_pause();
    }

    /// @brief Compiler-only reorder barrier — zero hardware instructions emitted.
    ///
    /// Prevents the compiler from moving memory accesses across this point.
    /// On x86 (TSO) this is sufficient to pin a MemCpy between two atomic
    /// counter stores in a SeqLock because the hardware already provides
    /// store-store and load-load ordering. On weakly-ordered architectures
    /// (ARM64, RISC-V) the surrounding Release/Acquire atomics supply the
    /// hardware fence; this barrier only stops the *compiler* from hoisting
    /// or sinking the MemCpy out of the critical window.
    ///
    /// Do NOT substitute this for a hardware fence in any context that
    /// requires cross-CPU visibility ordering.
    inline void CompilerBarrier() noexcept {
        __asm__ volatile("" ::: "memory");
    }

    /// @brief Issue a full memory barrier (acquire + release semantics).
    inline void FullMemoryBarrier() noexcept {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    }

    /// @brief Issue an acquire memory barrier.
    inline void AcquireBarrier() noexcept {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
    }

    /// @brief Issue a release memory barrier.
    inline void ReleaseBarrier() noexcept {
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }

    // ========================================================================
    // Atomic Operations Suite
    // ========================================================================

    /// @brief Memory ordering modes for atomic operations.
    enum class MemoryOrder : int {
        Relaxed   = __ATOMIC_RELAXED,
        Release   = __ATOMIC_RELEASE,
        Acquire   = __ATOMIC_ACQUIRE,
        AcqRel    = __ATOMIC_ACQ_REL,
        SeqCst    = __ATOMIC_SEQ_CST,
    };

    /// @brief Atomic load with specified memory ordering.
    template <typename T>
    inline T AtomicLoad(const T* ptr, int order) noexcept {
        return __atomic_load_n(ptr, order);
    }

    template <typename T>
    inline T AtomicLoad(const T* ptr, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_load_n(ptr, static_cast<int>(order));
    }

    /// @brief Atomic store with specified memory ordering.
    template <typename T>
    inline void AtomicStore(T* ptr, T value, int order) noexcept {
        __atomic_store_n(ptr, value, order);
    }

    template <typename T>
    inline void AtomicStore(T* ptr, T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        __atomic_store_n(ptr, value, static_cast<int>(order));
    }

    /// @brief Atomic exchange (swap) with sequential consistency.
    template <typename T>
    inline T AtomicExchange(T* ptr, T desired, int order) noexcept {
        return __atomic_exchange_n(ptr, desired, order);
    }

    template <typename T>
    inline T AtomicExchange(T* ptr, T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_exchange_n(ptr, desired, static_cast<int>(order));
    }

    /// @brief Atomic compare-and-swap.
    /// @param ptr Pointer to atomic value
    /// @param expected Expected value (updated if swap fails)
    /// @param desired Desired value
    /// @param weak Allow spurious failures (for weak CAS on ARM)
    /// @return true if swap succeeded, false if failed
    template <typename T>
    inline bool AtomicCompareExchange(
        T* ptr,
        T* expected,
        T desired,
        bool weak = false,
        int success = __ATOMIC_SEQ_CST,
        int failure = __ATOMIC_SEQ_CST) noexcept {
        return __atomic_compare_exchange_n(ptr, expected, desired, weak, success, failure);
    }

    template <typename T>
    inline bool AtomicCompareExchange(
        T* ptr,
        T* expected,
        T desired,
        bool weak = false,
        MemoryOrder success = MemoryOrder::SeqCst,
        MemoryOrder failure = MemoryOrder::SeqCst) noexcept {
        return __atomic_compare_exchange_n(
            ptr, expected, desired, weak,
            static_cast<int>(success),
            static_cast<int>(failure));
    }

    /// @brief Atomic fetch-and-add.
    /// @return The value BEFORE the addition
    template <typename T>
    inline T AtomicFetchAdd(T* ptr, T val, int order) noexcept {
        return __atomic_fetch_add(ptr, val, order);
    }

    template <typename T>
    inline T AtomicFetchAdd(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_fetch_add(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic fetch-and-subtract.
    /// @return The value BEFORE the subtraction
    template <typename T>
    inline T AtomicFetchSub(T* ptr, T val, int order) noexcept {
        return __atomic_fetch_sub(ptr, val, order);
    }

    template <typename T>
    inline T AtomicFetchSub(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_fetch_sub(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic fetch-and-bitwise-AND.
    /// @return The value BEFORE the AND operation
    template <typename T>
    inline T AtomicFetchAnd(T* ptr, T val, int order) noexcept {
        return __atomic_fetch_and(ptr, val, order);
    }

    template <typename T>
    inline T AtomicFetchAnd(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_fetch_and(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic fetch-and-bitwise-OR.
    /// @return The value BEFORE the OR operation
    template <typename T>
    inline T AtomicFetchOr(T* ptr, T val, int order) noexcept {
        return __atomic_fetch_or(ptr, val, order);
    }

    template <typename T>
    inline T AtomicFetchOr(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_fetch_or(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic fetch-and-bitwise-XOR.
    /// @return The value BEFORE the XOR operation
    template <typename T>
    inline T AtomicFetchXor(T* ptr, T val, int order) noexcept {
        return __atomic_fetch_xor(ptr, val, order);
    }

    template <typename T>
    inline T AtomicFetchXor(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_fetch_xor(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic add-and-fetch (add-then-fetch).
    /// @return The value AFTER the addition
    template <typename T>
    inline T AtomicAddFetch(T* ptr, T val, int order) noexcept {
        return __atomic_add_fetch(ptr, val, order);
    }

    template <typename T>
    inline T AtomicAddFetch(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_add_fetch(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic subtract-and-fetch.
    /// @return The value AFTER the subtraction
    template <typename T>
    inline T AtomicSubFetch(T* ptr, T val, int order) noexcept {
        return __atomic_sub_fetch(ptr, val, order);
    }

    template <typename T>
    inline T AtomicSubFetch(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_sub_fetch(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic AND-and-fetch.
    /// @return The value AFTER the AND operation
    template <typename T>
    inline T AtomicAndFetch(T* ptr, T val, int order) noexcept {
        return __atomic_and_fetch(ptr, val, order);
    }

    template <typename T>
    inline T AtomicAndFetch(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_and_fetch(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic OR-and-fetch.
    /// @return The value AFTER the OR operation
    template <typename T>
    inline T AtomicOrFetch(T* ptr, T val, int order) noexcept {
        return __atomic_or_fetch(ptr, val, order);
    }

    template <typename T>
    inline T AtomicOrFetch(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_or_fetch(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic XOR-and-fetch.
    /// @return The value AFTER the XOR operation
    template <typename T>
    inline T AtomicXorFetch(T* ptr, T val, int order) noexcept {
        return __atomic_xor_fetch(ptr, val, order);
    }

    template <typename T>
    inline T AtomicXorFetch(T* ptr, T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_xor_fetch(ptr, val, static_cast<int>(order));
    }

    /// @brief Atomic test-and-set (TAS) - sets byte to 1, returns old value.
    /// @desc Useful for simple spinlock primitives.
    /// @return true if the bit was already set, false if it was clear
    inline bool AtomicTestAndSet(volatile bool* ptr, int order) noexcept {
        return __atomic_test_and_set(ptr, order);
    }

    inline bool AtomicTestAndSet(volatile bool* ptr, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return __atomic_test_and_set(ptr, static_cast<int>(order));
    }

    /// @brief Atomic clear - sets byte to 0.
    /// @desc Complements AtomicTestAndSet for spinlock release.
    inline void AtomicClear(volatile bool* ptr, int order) noexcept {
        __atomic_clear(ptr, order);
    }

    inline void AtomicClear(volatile bool* ptr, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        __atomic_clear(ptr, static_cast<int>(order));
    }

    /// @brief In-Place Construction
#if FOUNDATIONKIT_COMPILER_HAS_BUILTIN(__builtin_construct_at)
    template <typename T, typename... Args>
    T* ConstructAt(void* ptr, Args&&... args) noexcept {
        T* p = static_cast<T*>(ptr);
        __builtin_construct_at(p, Forward<Args>(args)...);
        return p;
    }
#endif

} // namespace FoundationKitCxxStl::Base::CompilerBuiltins
