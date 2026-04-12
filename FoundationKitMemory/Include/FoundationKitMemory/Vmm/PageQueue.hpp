#pragma once

#include <FoundationKitMemory/Vmm/PageDescriptor.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PageQueue — LRU queue of PageDescriptors via Folio heads
    // =========================================================================

    /// @brief An intrusive doubly-linked queue of PageDescriptor nodes.
    ///
    /// @desc  Each queue corresponds to exactly one PageState value. Pages are
    ///        enqueued at the tail (cold end) and dequeued from the head (hot end).
    ///        The queue validates that every inserted page's state matches the
    ///        queue's designated state — inserting an Active page into the Free
    ///        queue is FK_BUG.
    ///
    ///        Operations are O(1). The queue does NOT own the pages.
    class PageQueue {
    public:
        /// @param queue_state  The PageState this queue represents.
        explicit constexpr PageQueue(PageState queue_state) noexcept
            : m_queue_state(queue_state) {}

        PageQueue(const PageQueue&)            = delete;
        PageQueue& operator=(const PageQueue&) = delete;

        // ----------------------------------------------------------------
        // Enqueue / Dequeue
        // ----------------------------------------------------------------

        /// @brief Add a page to the tail of this queue (cold end).
        ///
        /// @desc  The page's state must already be set to this queue's state
        ///        via TransitionTo() before calling Enqueue. Enqueue does NOT
        ///        set the state — it only validates it.
        void Enqueue(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr,
                "PageQueue::Enqueue: null page");
            FK_BUG_ON(page->IsTail(),
                "PageQueue::Enqueue: cannot enqueue a tail page (pfn={}) — enqueue the head page",
                page->pfn.value);
            FK_BUG_ON(page->state != m_queue_state,
                "PageQueue::Enqueue: page state ({}) does not match queue state ({}) (pfn={})",
                static_cast<u8>(page->state), static_cast<u8>(m_queue_state), page->pfn.value);

            m_list.PushBack(&page->lru);
        }

        /// @brief Remove and return the page at the head of the queue (hot end).
        /// @return Head PageDescriptor, or nullptr if queue is empty.
        [[nodiscard]] PageDescriptor* Dequeue() noexcept {
            if (m_list.Empty()) return nullptr;

            auto* node = m_list.PopFront();
            return ContainerOfLru(node);
        }

        /// @brief Remove a specific page from anywhere in the queue. O(1).
        void Remove(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr,
                "PageQueue::Remove: null page");
            FK_BUG_ON(page->state != m_queue_state,
                "PageQueue::Remove: page state ({}) does not match queue state ({}) (pfn={}) — "
                "page is not in this queue",
                static_cast<u8>(page->state), static_cast<u8>(m_queue_state), page->pfn.value);

            m_list.Remove(&page->lru);
        }

        // ----------------------------------------------------------------
        // Scan / Peek
        // ----------------------------------------------------------------

        /// @brief Peek at the head page without removing it.
        [[nodiscard]] PageDescriptor* PeekHead() const noexcept {
            if (m_list.Empty()) return nullptr;
            return ContainerOfLru(m_list.Begin());
        }

        /// @brief Walk all pages in queue order (head-to-tail).
        /// @param func  Callable with signature void(PageDescriptor*) noexcept.
        ///              Do NOT modify the queue inside func.
        template <typename Func>
        void ForEach(Func&& func) const noexcept {
            auto* node = m_list.Begin();
            auto* sentinel = const_cast<Structure::IntrusiveDoublyLinkedListNode*>(
                reinterpret_cast<const Structure::IntrusiveDoublyLinkedListNode*>(&m_list));
            while (node != sentinel) {
                auto* next = node->next;
                func(ContainerOfLru(node));
                node = next;
            }
        }

        // ----------------------------------------------------------------
        // Stats
        // ----------------------------------------------------------------

        [[nodiscard]] usize Count() const noexcept { return m_list.Size(); }
        [[nodiscard]] bool  Empty() const noexcept { return m_list.Empty(); }
        [[nodiscard]] PageState QueueState() const noexcept { return m_queue_state; }

    private:
        /// @brief Recover the PageDescriptor from its lru node pointer.
        [[nodiscard]] static PageDescriptor* ContainerOfLru(
                Structure::IntrusiveDoublyLinkedListNode* node) noexcept {
            FK_BUG_ON(node == nullptr, "PageQueue::ContainerOfLru: null node");
            return Structure::ContainerOf<PageDescriptor,
                &PageDescriptor::lru>(node);
        }

        Structure::IntrusiveDoublyLinkedList m_list;
        PageState m_queue_state;
    };

    // =========================================================================
    // PageQueueSet — manages the four canonical page queues
    // =========================================================================

    /// @brief Aggregates the Free, Active, Inactive, Wired, and Laundry queues.
    ///
    /// @desc  Provides high-level transition methods that atomically:
    ///        1. Remove the page from its current queue.
    ///        2. Execute the state transition (with FK_BUG_ON validation).
    ///        3. Enqueue the page into the target queue.
    ///
    ///        This ensures queue membership and page state are always consistent.
    ///        A page whose state is Active is ALWAYS in m_active, never in m_free.
    class PageQueueSet {
    public:
        constexpr PageQueueSet() noexcept
            : m_free(PageState::Free)
            , m_active(PageState::Active)
            , m_inactive(PageState::Inactive)
            , m_wired(PageState::Wired)
            , m_laundry(PageState::Laundry)
        {}

        PageQueueSet(const PageQueueSet&)            = delete;
        PageQueueSet& operator=(const PageQueueSet&) = delete;

        // ----------------------------------------------------------------
        // Initial placement (boot time — page starts in a queue)
        // ----------------------------------------------------------------

        /// @brief Place a newly initialized Free page into the free queue.
        /// @desc  Called during boot when populating the PageDescriptorArray.
        void EnqueueFree(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::EnqueueFree: null page");
            FK_BUG_ON(page->state != PageState::Free,
                "PageQueueSet::EnqueueFree: page is not Free (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->state));
            m_free.Enqueue(page);
        }

        // ----------------------------------------------------------------
        // State transitions
        // ----------------------------------------------------------------

        /// @brief Free → Active. Allocation path.
        void Activate(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Activate: null page");
            QueueForState(page->state).Remove(page);
            page->TransitionTo(PageState::Active);
            m_active.Enqueue(page);
        }

        /// @brief Active → Inactive. LRU scanner demotion.
        void Deactivate(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Deactivate: null page");
            FK_BUG_ON(page->state != PageState::Active,
                "PageQueueSet::Deactivate: page is not Active (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->state));
            m_active.Remove(page);
            page->TransitionTo(PageState::Inactive);
            m_inactive.Enqueue(page);
        }

        /// @brief Inactive → Active. Re-reference path.
        void Reactivate(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Reactivate: null page");
            FK_BUG_ON(page->state != PageState::Inactive,
                "PageQueueSet::Reactivate: page is not Inactive (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->state));
            m_inactive.Remove(page);
            page->TransitionTo(PageState::Active);
            m_active.Enqueue(page);
        }

        /// @brief Any → Wired. Pin path (kernel stacks, DMA, slab).
        void Wire(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Wire: null page");
            QueueForState(page->state).Remove(page);
            page->TransitionTo(PageState::Wired);
            m_wired.Enqueue(page);
        }

        /// @brief Wired → Active. Unpin path.
        void Unwire(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Unwire: null page");
            FK_BUG_ON(page->state != PageState::Wired,
                "PageQueueSet::Unwire: page is not Wired (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->state));
            m_wired.Remove(page);
            page->TransitionTo(PageState::Active);
            m_active.Enqueue(page);
        }

        /// @brief Inactive → Laundry. Dirty page selected for writeback.
        void LaunderPage(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::LaunderPage: null page");
            FK_BUG_ON(page->state != PageState::Inactive,
                "PageQueueSet::LaunderPage: page is not Inactive (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->state));
            FK_BUG_ON(!page->TestFlag(PageFlags::Dirty),
                "PageQueueSet::LaunderPage: page is not dirty (pfn={}) — "
                "only dirty pages go to laundry",
                page->pfn.value);
            m_inactive.Remove(page);
            page->TransitionTo(PageState::Laundry);
            page->SetFlag(PageFlags::Writeback);
            m_laundry.Enqueue(page);
        }

        /// @brief Any → Free. Deallocation / eviction path.
        void Free(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Free: null page");
            FK_BUG_ON(page->TestFlag(PageFlags::Locked),
                "PageQueueSet::Free: cannot free a locked page (pfn={})",
                page->pfn.value);

            QueueForState(page->state).Remove(page);
            page->TransitionTo(PageState::Free);
            page->ClearFlag(PageFlags::Dirty);
            page->ClearFlag(PageFlags::Referenced);
            page->ClearFlag(PageFlags::Writeback);
            page->ClearOwner();
            m_free.Enqueue(page);
        }

        /// @brief Pop a page from the free queue for allocation. Returns nullptr if empty.
        [[nodiscard]] PageDescriptor* AllocateFromFree() noexcept {
            return m_free.Dequeue();
        }

        // ----------------------------------------------------------------
        // LRU Scan — clock algorithm for eviction candidates
        // ----------------------------------------------------------------

        /// @brief Scan the inactive queue for eviction candidates.
        ///
        /// @param target        Maximum number of pages to collect.
        /// @param candidates    Output array to fill with eviction candidates.
        /// @param max_candidates Size of the candidates array.
        ///
        /// @return Number of candidates placed in the array.
        ///
        /// @desc  Implements a simplified clock algorithm:
        ///        - Walk inactive queue head-to-tail.
        ///        - If page has Referenced flag: clear it, move to tail (second chance).
        ///        - If page is clean and unreferenced: eviction candidate.
        ///        - If page is dirty and unreferenced: send to laundry.
        ///        - Stop when target is reached or queue exhausted.
        [[nodiscard]] usize ScanInactive(usize target,
                                          PageDescriptor** candidates,
                                          usize max_candidates) noexcept {
            FK_BUG_ON(candidates == nullptr && max_candidates > 0,
                "PageQueueSet::ScanInactive: null candidates array with non-zero max");

            const usize limit = target < max_candidates ? target : max_candidates;
            usize found = 0;
            usize scanned = 0;
            const usize queue_size = m_inactive.Count();

            // We scan at most queue_size entries to avoid infinite loops
            // (since we re-enqueue referenced pages at the tail).
            while (found < limit && scanned < queue_size) {
                PageDescriptor* page = m_inactive.Dequeue();
                if (!page) break;

                ++scanned;

                if (page->TestFlag(PageFlags::Locked)) {
                    // Locked pages cannot be evicted. Put back at tail.
                    page->state = PageState::Inactive; // Maintain state for re-enqueue
                    m_inactive.Enqueue(page);
                    continue;
                }

                if (page->TestFlag(PageFlags::Referenced)) {
                    // Second chance: clear referenced, put at tail (cold end).
                    page->ClearFlag(PageFlags::Referenced);
                    page->state = PageState::Inactive;
                    m_inactive.Enqueue(page);
                    continue;
                }

                if (page->TestFlag(PageFlags::Dirty)) {
                    // Dirty unreferenced page — send to laundry for writeback.
                    page->TransitionTo(PageState::Laundry);
                    page->SetFlag(PageFlags::Writeback);
                    m_laundry.Enqueue(page);
                    continue;
                }

                // Clean, unreferenced, unlocked page — eviction candidate.
                candidates[found++] = page;
                // Note: page is removed from inactive queue (Dequeue did that).
                // Caller is responsible for transitioning to Free after unmapping.
            }

            return found;
        }

        // ----------------------------------------------------------------
        // Proactive demotion — move cold Active pages to Inactive
        // ----------------------------------------------------------------

        /// @brief Scan the active queue and demote pages that lack the Referenced flag.
        ///
        /// @param target  Maximum number of pages to demote.
        /// @return Number of pages actually demoted.
        [[nodiscard]] usize ScanActive(usize target) noexcept {
            usize demoted = 0;
            usize scanned = 0;
            const usize queue_size = m_active.Count();

            while (demoted < target && scanned < queue_size) {
                PageDescriptor* page = m_active.Dequeue();
                if (!page) break;

                ++scanned;

                if (page->TestFlag(PageFlags::Referenced)) {
                    // Recently accessed — keep active, clear flag for next round.
                    page->ClearFlag(PageFlags::Referenced);
                    page->state = PageState::Active;
                    m_active.Enqueue(page);
                    continue;
                }

                // Not referenced — demote to inactive.
                page->TransitionTo(PageState::Inactive);
                m_inactive.Enqueue(page);
                ++demoted;
            }

            return demoted;
        }

        // ----------------------------------------------------------------
        // Queue access
        // ----------------------------------------------------------------

        [[nodiscard]] PageQueue& FreeQueue()     noexcept { return m_free; }
        [[nodiscard]] PageQueue& ActiveQueue()   noexcept { return m_active; }
        [[nodiscard]] PageQueue& InactiveQueue() noexcept { return m_inactive; }
        [[nodiscard]] PageQueue& WiredQueue()    noexcept { return m_wired; }
        [[nodiscard]] PageQueue& LaundryQueue()  noexcept { return m_laundry; }

        [[nodiscard]] const PageQueue& FreeQueue()     const noexcept { return m_free; }
        [[nodiscard]] const PageQueue& ActiveQueue()   const noexcept { return m_active; }
        [[nodiscard]] const PageQueue& InactiveQueue() const noexcept { return m_inactive; }
        [[nodiscard]] const PageQueue& WiredQueue()    const noexcept { return m_wired; }
        [[nodiscard]] const PageQueue& LaundryQueue()  const noexcept { return m_laundry; }

        // ----------------------------------------------------------------
        // Statistics
        // ----------------------------------------------------------------

        [[nodiscard]] usize FreeCount()     const noexcept { return m_free.Count(); }
        [[nodiscard]] usize ActiveCount()   const noexcept { return m_active.Count(); }
        [[nodiscard]] usize InactiveCount() const noexcept { return m_inactive.Count(); }
        [[nodiscard]] usize WiredCount()    const noexcept { return m_wired.Count(); }
        [[nodiscard]] usize LaundryCount()  const noexcept { return m_laundry.Count(); }

        [[nodiscard]] usize TotalManaged() const noexcept {
            return FreeCount() + ActiveCount() + InactiveCount()
                   + WiredCount() + LaundryCount();
        }

    private:
        /// @brief Get the queue that corresponds to the given state.
        [[nodiscard]] PageQueue& QueueForState(PageState s) noexcept {
            switch (s) {
                case PageState::Free:     return m_free;
                case PageState::Active:   return m_active;
                case PageState::Inactive: return m_inactive;
                case PageState::Wired:    return m_wired;
                case PageState::Laundry:  return m_laundry;
            }
            FK_BUG("PageQueueSet::QueueForState: unknown state {}", static_cast<u8>(s));
            // Unreachable, but silences compiler warning.
            return m_free;
        }

        PageQueue m_free;
        PageQueue m_active;
        PageQueue m_inactive;
        PageQueue m_wired;
        PageQueue m_laundry;
    };

} // namespace FoundationKitMemory
