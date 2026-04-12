#pragma once

#include <FoundationKitCxxStl/Sync/AtomicFlag.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl::Base;

    /// ============================================================================
    /// SpinLock: Atomic-based locking for SMP environments.
    /// @desc Does not depend on OSL scheduler/mutexes. Works in ISRs.
    /// ============================================================================
    class SpinLock {
    public:
        constexpr SpinLock() noexcept = default;

        void Lock() noexcept {
            while (m_flag.TestAndSet(MemoryOrder::Acquire)) {
                CompilerBuiltins::CpuPause();
            }
        }

        void Unlock() noexcept {
            FK_BUG_ON(!m_flag.Test(MemoryOrder::Relaxed),
                "SpinLock::Unlock: unlocking a lock that is not held");
            m_flag.Clear(MemoryOrder::Release);
        }

        bool TryLock() noexcept {
            return !m_flag.TestAndSet(MemoryOrder::Acquire);
        }

    private:
        AtomicFlag m_flag;
    };

} // namespace FoundationKitCxxStl::Sync
