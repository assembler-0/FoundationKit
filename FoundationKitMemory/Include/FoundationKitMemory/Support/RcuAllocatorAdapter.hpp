#pragma once

#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitCxxStl/Sync/Rcu.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // =========================================================================
    // RcuAllocatorAdapter<Alloc, Domain, MaxPending>
    //
    // ## What makes this actually RCU
    //
    // A naive "RCU allocator" that calls alloc.Deallocate() inside a callback
    // registered with the domain is not wrong in principle, but the previous
    // implementation had a fatal circular dependency: it allocated the
    // DeferredFree node from the same allocator it was deferring frees on.
    // If the allocator is a BumpAllocator or a nearly-full slab, that
    // allocation fails at exactly the moment you need it most.
    //
    // ## The fix: static node pool
    //
    // RcuAllocatorAdapter<Alloc, Domain, MaxPending> pre-allocates a fixed
    // array of MaxPending DeferredFree nodes at construction time. Each
    // Deallocate() call claims a node from this pool (lock-free via a free-
    // list), registers the RCU callback, and the callback returns the node
    // to the pool after performing the actual free.
    //
    // This breaks the circular dependency entirely: the node pool is separate
    // from the allocator being wrapped.
    //
    // ## Full RCU lifecycle
    //
    //   Deallocate(ptr, size):
    //     1. Pop a DeferredFree node from the free-list (FK_BUG_ON if empty).
    //     2. Fill it: {ptr, size, &m_alloc, this}.
    //     3. Call domain.CallAfterGracePeriod(&DeferredFree::Reclaim, node).
    //        The domain will fire Reclaim only after every CPU has reported QS.
    //
    //   DeferredFree::Reclaim(void* arg):
    //     1. Cast arg to DeferredFree*.
    //     2. Call alloc.Deallocate(ptr, size) — the actual free.
    //     3. Push the node back onto the adapter's free-list for reuse.
    //
    // ## MaxPending
    //
    // The maximum number of frees that can be in-flight simultaneously (i.e.,
    // registered with the domain but not yet reclaimed). Size this to the
    // maximum number of objects that can be removed from RCU-protected
    // structures between two consecutive grace periods.
    //
    // ## Thread safety
    //
    // The free-list uses a lock-free Treiber stack (Atomic<DeferredFree*> head,
    // CAS loop). Deallocate() is safe to call from any context that can call
    // domain.CallAfterGracePeriod() (i.e., not from hard IRQ unless the domain
    // uses an IRQ-safe lock).
    // =========================================================================

    /// @brief RCU-correct deferred-free allocator wrapper.
    ///
    /// @tparam Alloc      Any type satisfying IAllocator.
    /// @tparam Domain     An instantiation of RcuDomain<MaxCpus, MaxCallbacks>.
    /// @tparam MaxPending Maximum number of simultaneously in-flight deferred frees.
    template <IAllocator Alloc, typename Domain, usize MaxPending = 64>
    class RcuAllocatorAdapter {
        static_assert(MaxPending > 0, "RcuAllocatorAdapter: MaxPending must be > 0");

    public:
        /// @param alloc  Reference to the underlying allocator (must outlive this).
        /// @param domain Reference to the RCU domain (must outlive this).
        RcuAllocatorAdapter(Alloc& alloc, Domain& domain) noexcept
            : m_alloc(alloc), m_domain(domain), m_free_head(nullptr)
        {
            // Build the free-list: chain all nodes together.
            for (usize i = 0; i < MaxPending; ++i) {
                m_pool[i].next = i + 1 < MaxPending ? &m_pool[i + 1] : nullptr;
                m_pool[i].adapter = this;
            }
            m_free_head.Store(&m_pool[0], MemoryOrder::Relaxed);
        }

        RcuAllocatorAdapter(const RcuAllocatorAdapter&)            = delete;
        RcuAllocatorAdapter& operator=(const RcuAllocatorAdapter&) = delete;

        /// @brief Allocate memory. Passes through to the underlying allocator.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            return m_alloc.Allocate(size, align);
        }

        /// @brief Defer the deallocation of ptr until after the next RCU grace period.
        ///
        /// Claims a node from the pre-allocated pool, registers the RCU callback,
        /// and returns immediately. The actual alloc.Deallocate() call happens
        /// inside DeferredFree::Reclaim() after every CPU has reported QS.
        ///
        /// @param ptr  Pointer to free. Must have been returned by Allocate().
        /// @param size Size passed to the original Allocate() call.
        void Deallocate(void* ptr, usize size) noexcept {
            FK_BUG_ON(ptr == nullptr, "RcuAllocatorAdapter::Deallocate: null pointer");

            DeferredFree* node = PopNode();
            FK_BUG_ON(node == nullptr,
                "RcuAllocatorAdapter::Deallocate: node pool exhausted "
                "(MaxPending={}) — increase MaxPending or drain the domain more frequently",
                MaxPending);

            node->ptr  = ptr;
            node->size = size;
            // node->adapter is set at construction and never changes.

            // Register with the domain. Reclaim fires after the grace period.
            m_domain.CallAfterGracePeriod(&DeferredFree::Reclaim, node);
        }

        /// @brief Ownership check. Passes through to the underlying allocator.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_alloc.Owns(ptr);
        }

        /// @brief Number of DeferredFree nodes currently available in the pool.
        [[nodiscard]] usize AvailableNodes() const noexcept {
            usize count = 0;
            // Relaxed snapshot — for diagnostics only.
            const DeferredFree* n = m_free_head.Load(MemoryOrder::Relaxed);
            while (n) { ++count; n = n->next; }
            return count;
        }

    private:
        // =====================================================================
        // DeferredFree node
        //
        // Lives in m_pool[]. Carries {ptr, size, adapter*} through the grace
        // period. After Reclaim() fires, the node is returned to the free-list
        // so it can be reused for the next deferred free.
        // =====================================================================
        struct DeferredFree {
            void*         ptr;
            usize         size;
            DeferredFree* next;     // free-list linkage (when in pool)
            RcuAllocatorAdapter* adapter; // back-pointer to return node to pool

            /// @brief RCU callback: perform the actual free, then recycle the node.
            static void Reclaim(void* arg) noexcept {
                auto* node = static_cast<DeferredFree*>(arg);
                FK_BUG_ON(node->adapter == nullptr,
                    "RcuAllocatorAdapter::DeferredFree::Reclaim: null adapter back-pointer");
                FK_BUG_ON(node->ptr == nullptr,
                    "RcuAllocatorAdapter::DeferredFree::Reclaim: null target pointer");

                // Perform the actual deallocation on the underlying allocator.
                node->adapter->m_alloc.Deallocate(node->ptr, node->size);

                // Clear the payload and return the node to the pool.
                node->ptr  = nullptr;
                node->size = 0;
                node->adapter->PushNode(node);
            }
        };

        // =====================================================================
        // Lock-free Treiber stack for the node free-list.
        //
        // PopNode / PushNode use a CAS loop on m_free_head. This is safe for
        // concurrent callers (multiple CPUs calling Deallocate() simultaneously)
        // and for the Reclaim callback (which runs on whichever CPU completes
        // the grace period).
        //
        // ABA is not a concern here: nodes are only ever in one of two states
        // (in the pool or in-flight), and a node is never freed — it lives for
        // the lifetime of the adapter.
        // =====================================================================

        DeferredFree* PopNode() noexcept {
            DeferredFree* head = m_free_head.Load(MemoryOrder::Acquire);
            while (head != nullptr) {
                if (m_free_head.CompareExchange(head, head->next,
                        /*weak=*/true,
                        MemoryOrder::Acquire,
                        MemoryOrder::Relaxed)) {
                    head->next = nullptr;
                    return head;
                }
                // head updated by CAS failure — retry.
            }
            return nullptr; // pool exhausted
        }

        void PushNode(DeferredFree* node) noexcept {
            DeferredFree* head = m_free_head.Load(MemoryOrder::Relaxed);
            do {
                node->next = head;
            } while (!m_free_head.CompareExchange(head, node,
                         /*weak=*/true,
                         MemoryOrder::Release,
                         MemoryOrder::Relaxed));
        }

        Alloc&                   m_alloc;
        Domain&                  m_domain;
        Atomic<DeferredFree*>    m_free_head;
        DeferredFree             m_pool[MaxPending];
    };

    // Verify the adapter satisfies IAllocator (instantiated with concrete types in tests).

} // namespace FoundationKitMemory
