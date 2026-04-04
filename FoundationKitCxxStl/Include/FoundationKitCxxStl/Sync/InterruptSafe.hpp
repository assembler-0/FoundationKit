#pragma once

#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/TicketLock.hpp>

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
    ///       Thread-safe for SMP: the state is only written/read by the lock owner.
    /// ============================================================================
    template <BasicLockable LockType>
    class InterruptSafeLock {
    public:
        explicit InterruptSafeLock(LockType& lock) noexcept : m_lock(lock) {}

        /// @brief Default constructor for use in Synchronized<T, InterruptSafeLock<L>>.
        InterruptSafeLock() noexcept requires DefaultConstructible<LockType> : m_lock(m_owned_lock) {}

        void Lock() noexcept {
            uptr state = FoundationKitOsl::OslInterruptDisable();
            m_lock.Lock();
            m_state = state;
        }

        void Unlock() noexcept {
            uptr state = m_state;
            m_lock.Unlock();
            FoundationKitOsl::OslInterruptRestore(state);
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
        LockType  m_owned_lock; // Used if default-constructed
        uptr      m_state = 0;
    };

    /// @brief Convenient alias for a spinlock that disables interrupts.
    using InterruptSafeSpinLock = InterruptSafeLock<SpinLock>;

    /// @brief Convenient alias for a fair ticket lock that disables interrupts.
    using InterruptSafeTicketLock = InterruptSafeLock<TicketLock>;

} // namespace FoundationKitCxxStl::Sync
