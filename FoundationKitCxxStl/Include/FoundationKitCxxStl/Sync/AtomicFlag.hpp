#pragma once

#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitCxxStl::Sync {

    // =========================================================================
    // AtomicFlag
    //
    // A single-bit atomic boolean backed by __atomic_test_and_set /
    // __atomic_clear — the only two operations the C++ memory model guarantees
    // are always lock-free regardless of platform or sizeof(T).
    //
    // This is intentionally NOT a thin wrapper over Atomic<bool>. The
    // __atomic_test_and_set / __atomic_clear builtins operate on a byte that
    // the ABI defines as "the atomic flag byte" — its representation is
    // implementation-defined and may differ from a plain bool. Mixing
    // __atomic_test_and_set with __atomic_store_n on the same byte is
    // undefined behaviour per GCC's documentation.
    //
    // ## Why SpinLock should use this instead of volatile bool
    //
    // volatile bool + __atomic_test_and_set works in practice on x86/ARM but
    // is technically UB: volatile does not imply atomicity and the compiler is
    // free to generate a non-atomic read-modify-write for the TAS. AtomicFlag
    // uses the correct type (__atomic_flag_byte / unsigned char) and the
    // correct builtins, eliminating the UB.
    //
    // ## API contract
    //
    // TestAndSet(order) — sets the flag, returns the *previous* value.
    //                     Returns true if the flag was already set (lock busy).
    //                     Returns false if the flag was clear (lock acquired).
    // Clear(order)      — clears the flag. order must be Release or weaker;
    //                     SeqCst is accepted but AcqRel is illegal on a store
    //                     (FK_BUG_ON enforces this).
    // Test(order)       — non-modifying load of the current state.
    // =========================================================================

    /// @brief Lock-free single-bit flag. The only atomic type guaranteed
    ///        always lock-free by the C++ memory model.
    class AtomicFlag {
    public:
        constexpr AtomicFlag() noexcept : m_flag(false) {}

        AtomicFlag(const AtomicFlag&)            = delete;
        AtomicFlag& operator=(const AtomicFlag&) = delete;

        /// @brief Atomically set the flag and return its previous value.
        ///
        /// @param order  Must be Acquire, AcqRel, or SeqCst for a lock acquire.
        ///               Relaxed is legal but almost never correct for a spinlock.
        /// @return true  if the flag was already set (caller did NOT acquire).
        /// @return false if the flag was clear (caller acquired the flag).
        [[nodiscard]] bool TestAndSet(MemoryOrder order = MemoryOrder::SeqCst) noexcept {
            return CompilerBuiltins::AtomicTestAndSet(&m_flag, static_cast<int>(order));
        }

        /// @brief Atomically clear the flag.
        ///
        /// @param order  Must be Release, Relaxed, or SeqCst. AcqRel is
        ///               architecturally illegal on a store operation —
        ///               FK_BUG_ON catches this at runtime.
        void Clear(MemoryOrder order = MemoryOrder::Release) noexcept {
            FK_BUG_ON(order == MemoryOrder::AcqRel,
                "AtomicFlag::Clear: AcqRel is invalid for a store-only operation "
                "(no load half exists); use Release");
            CompilerBuiltins::AtomicClear(&m_flag, static_cast<int>(order));
        }

        /// @brief Non-modifying snapshot of the flag state.
        ///
        /// Uses an Acquire load so the caller sees all stores that happened
        /// before the flag was set. Use Relaxed only for diagnostics.
        [[nodiscard]] bool Test(MemoryOrder order = MemoryOrder::Acquire) const noexcept {
            // Cast away volatile before passing to AtomicLoad: the volatile
            // qualifier is a storage-side annotation that prevents the compiler
            // from caching the byte in a register. AtomicLoad returns a plain
            // value — returning volatile bool is deprecated in C++20/23 and
            // triggers -Wdeprecated-volatile on every translation unit that
            // includes this header.
            return CompilerBuiltins::AtomicLoad(
                const_cast<const bool*>(&m_flag),
                static_cast<int>(order));
        }

    private:
        volatile bool m_flag;
    };

} // namespace FoundationKitCxxStl::Sync
