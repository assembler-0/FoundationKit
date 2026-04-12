#pragma once

#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// MCSNode: Per-thread/CPU node for the MCS lock queue.
    /// @desc Must be allocated by the caller (usually on the stack or per-CPU).
    /// ============================================================================
    struct MCSNode {
        Atomic<MCSNode*> Next{nullptr};
        Atomic<bool>     Locked{false};

        constexpr MCSNode() noexcept = default;
    };

    /// ============================================================================
    /// MCSLock: A scalable, queue-based spinlock (Mellor-Crummey and Scott).
    /// @desc Excellent for many-core systems as it minimizes cache-line bouncing.
    ///       Each processor spins on its own local MCSNode.
    /// @warning NOT compatible with standard LockGuard directly as it requires a node.
    /// ============================================================================
    class MCSLock {
    public:
        constexpr MCSLock() noexcept = default;

        /// @brief Acquire the lock using a locally provided node.
        /// @param node Reference to a node (must remain valid until Unlock).
        void Lock(MCSNode& node) noexcept {
            node.Next.Store(nullptr, MemoryOrder::Relaxed);
            FK_BUG_ON(node.Locked.Load(MemoryOrder::Relaxed),
                "MCSLock::Lock: node is already in the locked state (node reuse before previous cycle completed)");
            node.Locked.Store(true, MemoryOrder::Relaxed);

            MCSNode* prev = m_tail.Exchange(&node, MemoryOrder::AcqRel);
            if (prev != nullptr) {
                prev->Next.Store(&node, MemoryOrder::Release);
                while (node.Locked.Load(MemoryOrder::Acquire)) {
                    CompilerBuiltins::CpuPause();
                }
            }
        }

        /// @brief Release the lock using the same node provided to Lock.
        /// @param node Reference to the node used during acquisition.
        void Unlock(MCSNode& node) noexcept {
            FK_BUG_ON(m_tail.Load(MemoryOrder::Relaxed) == nullptr,
                "MCSLock::Unlock: tail is null (unlock called without a prior Lock)");
            if (node.Next.Load(MemoryOrder::Relaxed) == nullptr) {
                MCSNode* expected = &node;
                if (m_tail.CompareExchange(expected, nullptr, true, MemoryOrder::Release)) {
                    // Successfully cleared the tail, no one else is waiting.
                    return;
                }
                
                while (node.Next.Load(MemoryOrder::Acquire) == nullptr) {
                    CompilerBuiltins::CpuPause();
                }
            }
            
            node.Next.Load(MemoryOrder::Relaxed)->Locked.Store(false, MemoryOrder::Release);
        }

    private:
        Atomic<MCSNode*> m_tail{nullptr};
    };

} // namespace FoundationKitCxxStl::Sync
