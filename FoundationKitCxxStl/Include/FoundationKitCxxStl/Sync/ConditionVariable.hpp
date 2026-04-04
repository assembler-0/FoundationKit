#pragma once

#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// ConditionVariable: Synchronization primitive for waiting on condition.
    /// @desc Threads wait on the CV until notified. Requires a Mutex.
    /// ============================================================================
    class ConditionVariable {
    public:
        constexpr ConditionVariable() noexcept = default;

        /// @brief Wait on the condition.
        /// @param lock The unique lock protecting the state.
        void Wait(UniqueLock<Mutex>& lock) noexcept {
            if (!lock.IsOwned()) return;

            // Release lock and sleep. OSL must handle the wait channel.
            lock.Unlock();
            FoundationKitOsl::OslThreadSleep(this);
            lock.Lock();
        }

        /// @brief Notify one waiting thread.
        void NotifyOne() noexcept {
            FoundationKitOsl::OslThreadWake(this);
        }

        /// @brief Notify all waiting threads.
        void NotifyAll() noexcept {
            FoundationKitOsl::OslThreadWakeAll(this);
        }
    };

} // namespace FoundationKitCxxStl::Sync
