#pragma once

#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// TicketLock: A fair spinlock using ticket-based ordering.
    /// @desc Ensures FIFO ordering for lock acquisition, preventing starvation.
    /// ============================================================================
    class TicketLock {
    public:
        constexpr TicketLock() noexcept = default;

        void Lock() noexcept {
            const u32 ticket = m_next_ticket.FetchAdd(1, MemoryOrder::Relaxed);
            while (m_now_serving.Load(MemoryOrder::Acquire) != ticket) {
                CompilerBuiltins::CpuPause();
            }
        }

        void Unlock() noexcept {
            const u32 current = m_now_serving.Load(MemoryOrder::Relaxed);
            // If now_serving == next_ticket the queue is empty — unlocking again is a bug.
            FK_BUG_ON(current == m_next_ticket.Load(MemoryOrder::Relaxed),
                "TicketLock::Unlock: no ticket is being served (unlock without matching lock)");
            m_now_serving.Store(current + 1, MemoryOrder::Release);
        }

        bool TryLock() noexcept {
            u32 ticket = m_now_serving.Load(MemoryOrder::Relaxed);
            return m_next_ticket.CompareExchange(ticket, ticket + 1, false, MemoryOrder::Acquire, MemoryOrder::Relaxed);
        }

    private:
        Atomic<u32> m_now_serving{0};
        Atomic<u32> m_next_ticket{0};
    };

} // namespace FoundationKitCxxStl::Sync
