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
            // Setting Locked=true marks this node as "waiting"; the predecessor
            // will clear it when passing the lock. If it is already true here,
            // the same node is being reused before the previous Lock/Unlock cycle
            // completed — a serious concurrency bug.
            FK_BUG_ON(node.Locked.Load(MemoryOrder::Relaxed),
                "MCSLock::Lock: node is already in the locked state (node reuse before previous cycle completed)");
            node.Locked.Store(true, MemoryOrder::Relaxed);

            MCSNode* prev = m_tail.Exchange(&node, MemoryOrder::Acquire);
            if (prev != nullptr) {
                // Someone else is holding the lock or waiting.
                // Join the queue by pointing the previous tail to us.
                prev->Next.Store(&node, MemoryOrder::Release);
                
                // Spin locally on our own 'Locked' flag.
                while (node.Locked.Load(MemoryOrder::Acquire)) {
                    CompilerBuiltins::CpuPause();
                }
            }
            // If prev == nullptr, we are the first in the queue and hold the lock.
        }

        /// @brief Release the lock using the same node provided to Lock.
        /// @param node Reference to the node used during acquisition.
        void Unlock(MCSNode& node) noexcept {
            // If the tail is not our node and our Next is null, we are not the
            // current lock holder — unlock without a matching lock.
            FK_BUG_ON(m_tail.Load(MemoryOrder::Relaxed) == nullptr,
                "MCSLock::Unlock: tail is null (unlock called without a prior Lock)");
            if (node.Next.Load(MemoryOrder::Relaxed) == nullptr) {
                // We seem to be at the tail of the queue.
                MCSNode* expected = &node;
                if (m_tail.CompareExchange(expected, nullptr, true, MemoryOrder::Release)) {
                    // Successfully cleared the tail, no one else is waiting.
                    return;
                }
                
                // Someone is in the process of joining the queue.
                // Wait for their pointer to be stored in our 'Next'.
                while (node.Next.Load(MemoryOrder::Acquire) == nullptr) {
                    CompilerBuiltins::CpuPause();
                }
            }
            
            // Pass the lock to the next waiter in the queue.
            node.Next.Load(MemoryOrder::Relaxed)->Locked.Store(false, MemoryOrder::Release);
        }

    private:
        Atomic<MCSNode*> m_tail{nullptr};
    };

} // namespace FoundationKitCxxStl::Sync
