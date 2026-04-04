#pragma once

#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// InterruptGuard: RAII wrapper to disable interrupts on current CPU.
    /// ============================================================================
    class InterruptGuard {
    public:
        InterruptGuard() noexcept : m_state(FoundationKitOsl::OslInterruptDisable()) {}
        ~InterruptGuard() noexcept { FoundationKitOsl::OslInterruptRestore(m_state); }

        // Non-copyable
        InterruptGuard(const InterruptGuard&) = delete;
        InterruptGuard& operator=(const InterruptGuard&) = delete;

    private:
        uptr m_state;
    };

    /// ============================================================================
    /// InterruptSafeLock: Combines a lock with interrupt disabling.
    /// @desc Ensures that the current CPU does not get interrupted while holding the lock.
    /// ============================================================================
    template <BasicLockable LockType>
    class InterruptSafeLock {
    public:
        explicit InterruptSafeLock(LockType& lock) noexcept : m_lock(lock) {}

        void Lock() noexcept {
            m_state = FoundationKitOsl::OslInterruptDisable();
            m_lock.Lock();
        }

        void Unlock() noexcept {
            m_lock.Unlock();
            FoundationKitOsl::OslInterruptRestore(m_state);
        }

        bool TryLock() noexcept requires Lockable<LockType> {
            uptr state = FoundationKitOsl::OslInterruptDisable();
            if (m_lock.TryLock()) {
                m_state = state;
                return true;
            }
            FoundationKitOsl::OslInterruptRestore(state);
            return false;
        }

    private:
        LockType& m_lock;
        uptr      m_state = 0;
    };

} // namespace FoundationKitCxxStl::Sync
