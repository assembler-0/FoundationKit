#pragma once

#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// Mutex: A blocking mutual exclusion lock.
    /// @desc Uses OSL scheduling to sleep the thread when the lock is contended.
    /// ============================================================================
    class Mutex {
    public:
        constexpr Mutex() noexcept = default;

        void Lock() noexcept {
            // Spin a bit first to avoid expensive context switch for short waits
            for (u32 i = 0; i < 100; ++i) {
                if (TryLock()) return;
                CompilerBuiltins::CpuPause();
            }

            while (m_locked.Exchange(true, MemoryOrder::Acquire)) {
                FoundationKitOsl::OslThreadSleep(this);
            }
        }

        void Unlock() noexcept {
            m_locked.Store(false, MemoryOrder::Release);
            FoundationKitOsl::OslThreadWake(this);
        }

        bool TryLock() noexcept {
            return !m_locked.Exchange(true, MemoryOrder::Acquire);
        }

    private:
        Atomic<bool> m_locked{false};
    };

} // namespace FoundationKitCxxStl::Sync
