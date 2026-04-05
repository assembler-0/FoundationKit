#pragma once

#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

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
            while (CompilerBuiltins::AtomicTestAndSet(&m_locked, __ATOMIC_ACQUIRE)) {
                CompilerBuiltins::CpuPause();
            }
        }

        void Unlock() noexcept {

            CompilerBuiltins::AtomicClear(&m_locked, __ATOMIC_RELEASE);
        }

        bool TryLock() noexcept {
            return !CompilerBuiltins::AtomicTestAndSet(&m_locked, __ATOMIC_ACQUIRE);
        }

    private:
        volatile bool m_locked = false;
    };

} // namespace FoundationKitCxxStl::Sync
