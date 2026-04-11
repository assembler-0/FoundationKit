#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // =========================================================================
    // RcuListNode / RcuList<T, Member, Domain>
    //
    // ## What makes this actually RCU
    //
    // Three things must all be present for a list to be RCU-correct:
    //
    //   1. ATOMIC NEXT — readers traverse `next` with Acquire loads. A plain
    //      pointer write is not sufficient: the CPU or compiler may reorder the
    //      pointer store before the stores that initialise the new node's data,
    //      causing a reader to see a partially-constructed node.
    //
    //   2. DEFERRED RECLAMATION — Remove() does NOT free the node. It calls
    //      domain.CallAfterGracePeriod(reclaim, obj) so the node stays alive
    //      until every CPU has passed a quiescent state. A reader that loaded
    //      the node's address before the Remove() Release-store will still
    //      safely dereference it during its current read-side critical section.
    //
    //   3. GRACE PERIOD ENFORCEMENT — the domain's ReportQs() / Synchronize()
    //      machinery guarantees that the reclaim callback fires only after all
    //      readers that could have seen the old pointer have passed a QS.
    //
    // ## Writer serialization
    //
    // PushFront / PushBack / Remove must be called under an external writer
    // lock. RCU protects readers from writers, not writers from each other.
    //
    // ## `prev` pointer
    //
    // `prev` is a plain (non-atomic) pointer used only by writers for O(1)
    // removal. Readers never touch `prev`. Making it atomic would add a second
    // Acquire load per traversal step with no benefit.
    //
    // ## Traversal
    //
    // ForEach() and FindFirst() follow `next` with Acquire loads. They must be
    // called inside an RcuReadLock scope. A node being concurrently removed is
    // still visible to readers that loaded its address before the Remove()
    // Release-store — this is correct QSBR behaviour, not a bug.
    //
    // ## Sentinel head
    //
    // The list uses a sentinel head node. Empty list: head.next == &head.
    // =========================================================================

    /// @brief Intrusive list node for RCU-safe traversal.
    struct RcuListNode {
        /// next is Atomic so readers can load it with Acquire ordering.
        /// Writers store with Release to make the new linkage visible atomically.
        Atomic<RcuListNode*> next;

        /// prev is plain — only writers touch it, always under a lock.
        RcuListNode* prev;

        constexpr RcuListNode() noexcept : next(this), prev(this) {}

        [[nodiscard]] bool IsLinked() const noexcept {
            return next.Load(MemoryOrder::Relaxed) != this;
        }
    };

    /// @brief RCU-safe intrusive doubly-linked list.
    ///
    /// @tparam T      Container type embedding an RcuListNode member.
    /// @tparam Member Pointer-to-member of the RcuListNode field within T.
    /// @tparam Domain An instantiation of RcuDomain<MaxCpus, MaxCallbacks>.
    template <typename T, RcuListNode T::*Member, typename Domain>
    class RcuList {
    public:
        /// @param domain The RCU domain that manages grace periods for this list.
        explicit RcuList(Domain& domain) noexcept : m_domain(domain) {}

        RcuList(const RcuList&)            = delete;
        RcuList& operator=(const RcuList&) = delete;

        // =====================================================================
        // Write-side (must be called under an external writer lock)
        // =====================================================================

        /// @brief Insert obj at the front of the list.
        ///
        /// Release store on head.next: readers that subsequently Acquire-load
        /// head.next will see the new node and all its data initialised.
        void PushFront(T* obj) noexcept {
            FK_BUG_ON(obj == nullptr, "RcuList::PushFront: null object");
            RcuListNode* node  = &(obj->*Member);
            FK_BUG_ON(node->IsLinked(), "RcuList::PushFront: node is already linked");

            RcuListNode* first = m_head.next.Load(MemoryOrder::Relaxed);
            node->prev         = &m_head;
            // Relaxed: node->next is not yet visible to readers (head.next still
            // points to `first`). The Release store on head.next below is the
            // visibility fence.
            node->next.Store(first, MemoryOrder::Relaxed);
            first->prev = node;
            // Release: makes node->data and node->next visible before the pointer
            // to node is exposed via head.next.
            m_head.next.Store(node, MemoryOrder::Release);
            ++m_size;
        }

        /// @brief Insert obj at the back of the list.
        void PushBack(T* obj) noexcept {
            FK_BUG_ON(obj == nullptr, "RcuList::PushBack: null object");
            RcuListNode* node = &(obj->*Member);
            FK_BUG_ON(node->IsLinked(), "RcuList::PushBack: node is already linked");

            RcuListNode* last = m_head.prev;
            node->prev        = last;
            node->next.Store(&m_head, MemoryOrder::Relaxed);
            m_head.prev       = node;
            // Release: makes the new tail visible to readers traversing forward.
            last->next.Store(node, MemoryOrder::Release);
            ++m_size;
        }

        /// @brief Unlink obj and defer its reclamation through the RCU domain.
        ///
        /// This is the core of RCU list correctness. The node is bypassed
        /// immediately (new readers skip it), but its memory is NOT freed until
        /// every CPU has passed a quiescent state. Readers that already loaded
        /// the node's address before this Release-store will safely dereference
        /// it for the remainder of their read-side critical section.
        ///
        /// @param obj     The object to remove. Must be currently linked.
        /// @param reclaim Called with obj after the grace period. Responsible
        ///                for destroying and freeing the object.
        void Remove(T* obj, void (*reclaim)(void*)) noexcept {
            FK_BUG_ON(obj == nullptr,    "RcuList::Remove: null object");
            FK_BUG_ON(reclaim == nullptr, "RcuList::Remove: null reclaim callback");
            RcuListNode* node = &(obj->*Member);
            FK_BUG_ON(!node->IsLinked(), "RcuList::Remove: node is not linked");
            FK_BUG_ON(m_size == 0,       "RcuList::Remove: size is zero (counter corruption)");

            RcuListNode* prev_node = node->prev;
            RcuListNode* next_node = node->next.Load(MemoryOrder::Relaxed);

            next_node->prev = prev_node;
            // Release: readers that Acquire-load prev_node->next after this store
            // will skip the removed node entirely. Readers that already loaded
            // node's address before this store will still see valid data — the
            // grace period guarantees node stays alive until they pass a QS.
            prev_node->next.Store(next_node, MemoryOrder::Release);

            // Reset to self-loop so IsLinked() returns false.
            node->next.Store(node, MemoryOrder::Relaxed);
            node->prev = node;
            --m_size;

            // Defer reclamation: the domain fires reclaim(obj) only after every
            // CPU has reported a quiescent state.
            m_domain.CallAfterGracePeriod(reclaim, obj);
        }

        // =====================================================================
        // Read-side (safe to call concurrently with writers, inside RcuReadLock)
        // =====================================================================

        /// @brief Call func(T&) for every node in the list.
        ///
        /// Must be called inside an RcuReadLock scope. Forward traversal only.
        template <Invocable<T&> Func>
        void ForEach(Func&& func) const noexcept {
            // Acquire: see the Release store from the most recent PushFront/Remove.
            const RcuListNode* node = m_head.next.Load(MemoryOrder::Acquire);
            while (node != &m_head) {
                T* obj = ContainerOf(node);
                func(*obj);
                // Acquire: if this node was concurrently removed, we see the
                // Release store that bypassed it and follow the updated next.
                node = node->next.Load(MemoryOrder::Acquire);
            }
        }

        /// @brief Return the first node satisfying pred(T&), or nullptr.
        ///
        /// Must be called inside an RcuReadLock scope.
        template <Predicate<T&> Pred>
        [[nodiscard]] T* FindFirst(Pred&& pred) const noexcept {
            const RcuListNode* node = m_head.next.Load(MemoryOrder::Acquire);
            while (node != &m_head) {
                T* obj = ContainerOf(node);
                if (pred(*obj)) return obj;
                node = node->next.Load(MemoryOrder::Acquire);
            }
            return nullptr;
        }

        [[nodiscard]] bool  Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()  const noexcept { return m_size; }

    private:
        /// @brief Recover the T* from a node pointer using the member offset.
        static T* ContainerOf(const RcuListNode* node) noexcept {
            // Compute the byte offset of Member within T using a non-null dummy.
            const T*   dummy  = reinterpret_cast<const T*>(0x1000);
            const uptr offset = reinterpret_cast<uptr>(&(dummy->*Member))
                              - reinterpret_cast<uptr>(dummy);
            return reinterpret_cast<T*>(reinterpret_cast<uptr>(node) - offset);
        }

        RcuListNode m_head;
        usize       m_size = 0;
        Domain&     m_domain;
    };

} // namespace FoundationKitCxxStl::Structure
