#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// SharedSpinLock: Multi-reader, single-writer spin-based lock.
    /// @desc Starvation-prone for writers if readers are constantly arriving.
    /// ============================================================================
    class SharedSpinLock {
    public:
        constexpr SharedSpinLock() noexcept = default;

        // Exclusive access
        void Lock() noexcept {
            u32 expected = 0;
            while (!m_state.CompareExchange(expected, WRITER_MASK, true, MemoryOrder::Acquire, MemoryOrder::Relaxed)) {
                expected = 0;
                CompilerBuiltins::CpuPause();
            }
        }


        bool TryLock() noexcept {
            u32 expected = 0;
            return m_state.CompareExchange(expected, WRITER_MASK, false, MemoryOrder::Acquire, MemoryOrder::Relaxed);
        }

        void Unlock() noexcept {
            m_state.Store(0, MemoryOrder::Release);
        }

        // Shared access
        void LockShared() noexcept {
            while (true) {
                u32 current = m_state.Load(MemoryOrder::Relaxed);
                if (current < WRITER_MASK) {
                    if (m_state.CompareExchange(current, current + 1, true, MemoryOrder::Acquire, MemoryOrder::Relaxed)) {
                        break;
                    }
                }
                CompilerBuiltins::CpuPause();
            }
        }

        bool TryLockShared() noexcept {
            u32 current = m_state.Load(MemoryOrder::Relaxed);
            if (current >= WRITER_MASK) return false;
            return m_state.CompareExchange(current, current + 1, false, MemoryOrder::Acquire, MemoryOrder::Relaxed);
        }

        void UnlockShared() noexcept {
            // Decrementing below zero means an extra UnlockShared was called — always a bug.
            FK_BUG_ON(m_state.Load(MemoryOrder::Relaxed) == 0,
                "SharedSpinLock::UnlockShared: reader count is already zero (extra UnlockShared)");
            m_state.FetchSub(1, MemoryOrder::Release);
        }

    private:
        static constexpr u32 WRITER_MASK = 0x80000000;
        Atomic<u32> m_state{0}; // 0: free, < WRITER_MASK: reader count, WRITER_MASK: writer
    };

} // namespace FoundationKitCxxStl::Sync
