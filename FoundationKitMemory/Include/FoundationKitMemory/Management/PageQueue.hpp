#pragma once

#include <FoundationKitMemory/Management/PageDescriptor.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

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
    ///        Count() is backed by Sync::Atomic<usize> so it can be read without
    ///        acquiring m_lock — readers get a consistent snapshot at the cost of
    ///        one possible transient off-by-one during concurrent mutation.
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
        // Lock exposure (for PageQueueSet's two-phase transition protocol)
        // ----------------------------------------------------------------

        /// @brief The per-queue spinlock.  Exposed so PageQueueSet can acquire
        ///        two different queue locks without nesting them.
        Sync::SpinLock m_lock;

        // ----------------------------------------------------------------
        // Locked public API
        // ----------------------------------------------------------------

        /// @brief Add a page to the tail of this queue (cold end).  Acquires lock.
        void Enqueue(PageDescriptor* page) noexcept {
            UniqueLock guard(m_lock);
            EnqueueUnlocked(page);
        }

        /// @brief Remove and return the page at the head (hot end).  Acquires lock.
        /// @return Head PageDescriptor, or nullptr if queue is empty.
        [[nodiscard]] PageDescriptor* Dequeue() noexcept {
            UniqueLock guard(m_lock);
            return DequeueUnlocked();
        }

        /// @brief Remove a specific page from anywhere in the queue. O(1).  Acquires lock.
        void Remove(PageDescriptor* page) noexcept {
            UniqueLock guard(m_lock);
            RemoveUnlocked(page);
        }

        // ----------------------------------------------------------------
        // Unlocked variants — caller MUST hold m_lock
        // ----------------------------------------------------------------

        /// @brief Enqueue without acquiring the lock.
        ///
        /// @desc  The page's state must already be set to this queue's state
        ///        via TransitionTo() before calling this. This validates but
        ///        does not set the state.
        void EnqueueUnlocked(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueue::EnqueueUnlocked: null page");
            FK_BUG_ON(page->IsTail(),
                "PageQueue::EnqueueUnlocked: cannot enqueue a tail page (pfn={}) — enqueue the head page",
                page->pfn.value);
            FK_BUG_ON(page->State() != m_queue_state,
                "PageQueue::EnqueueUnlocked: page state ({}) does not match queue state ({}) (pfn={})",
                static_cast<u8>(page->State()), static_cast<u8>(m_queue_state), page->pfn.value);

            m_list.PushBack(&page->lru);
            m_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Dequeue head without acquiring the lock.
        [[nodiscard]] PageDescriptor* DequeueUnlocked() noexcept {
            if (m_list.Empty()) return nullptr;
            auto* node = m_list.PopFront();
            m_count.FetchSub(1, Sync::MemoryOrder::Relaxed);
            return ContainerOfLru(node);
        }

        /// @brief Remove a specific page without acquiring the lock.
        void RemoveUnlocked(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueue::RemoveUnlocked: null page");
            FK_BUG_ON(page->State() != m_queue_state,
                "PageQueue::RemoveUnlocked: page state ({}) does not match queue state ({}) (pfn={}) — "
                "page is not in this queue",
                static_cast<u8>(page->State()), static_cast<u8>(m_queue_state), page->pfn.value);

            m_list.Remove(&page->lru);
            m_count.FetchSub(1, Sync::MemoryOrder::Relaxed);
        }

        // ----------------------------------------------------------------
        // Scan / Peek    (must be called under m_lock or as a snapshot read)
        // ----------------------------------------------------------------

        /// @brief Peek at the head page without removing it.  Caller must hold m_lock.
        [[nodiscard]] PageDescriptor* PeekHeadUnlocked() const noexcept {
            if (m_list.Empty()) return nullptr;
            return ContainerOfLru(m_list.Begin());
        }

        // ----------------------------------------------------------------
        // Stats
        // ----------------------------------------------------------------

        /// @brief Lock-free approximate count.
        [[nodiscard]] usize Count() const noexcept {
            return m_count.Load(Sync::MemoryOrder::Relaxed);
        }

        [[nodiscard]] bool  Empty() const noexcept { return Count() == 0; }
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

        /// @brief Lock-free approximate count.  Increment/decrement with Relaxed
        ///        ordering; they don't need sequential consistency because the
        ///        list structure itself is protected by m_lock.
        Sync::Atomic<usize> m_count{0};

        PageState m_queue_state;
    };

    // =========================================================================
    // PageQueueSet — manages the four canonical page queues
    // =========================================================================

    /// @brief Aggregates the Free, Active, Inactive, Wired, and Laundry queues.
    ///
    /// @desc  Provides high-level transition methods that atomically:
    ///        1. Remove the page from its current queue (under the source lock).
    ///        2. Execute the state transition (with FK_BUG_ON validation).
    ///        3. Enqueue the page into the target queue (under the dest lock).
    ///
    ///        This ensures queue membership and page state are always consistent.
    ///        A page whose state is Active is ALWAYS in m_active, never in m_free.
    ///        ABBA NOTE: We never hold two PageQueue locks simultaneously.
    ///                   The transition protocol is: (a) acquire source lock,
    ///                   remove from source and update state, release source lock;
    ///                   (b) acquire dest lock, enqueue in dest, release dest lock.
    ///                   This avoids ABBA deadlock with any order of two-queue
    ///                   operations by never holding two queue locks at once.
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
            FK_BUG_ON(page->State() != PageState::Free,
                "PageQueueSet::EnqueueFree: page is not Free (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->State()));
            m_free.Enqueue(page);
        }

        /// @brief Free → Active.  Allocation path.
        void Activate(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Activate: null page");
            {
                PageQueue& src = QueueForState(page->State());
                UniqueLock src_guard(src.m_lock);
                src.RemoveUnlocked(page);
                page->TransitionTo(PageState::Active);
            }
            {
                UniqueLock dst_guard(m_active.m_lock);
                m_active.EnqueueUnlocked(page);
            }
        }

        /// @brief Active → Inactive.  LRU scanner demotion.
        void Deactivate(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Deactivate: null page");
            FK_BUG_ON(page->State() != PageState::Active,
                "PageQueueSet::Deactivate: page is not Active (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->State()));
            {
                UniqueLock src_guard(m_active.m_lock);
                m_active.RemoveUnlocked(page);
                page->TransitionTo(PageState::Inactive);
            }
            {
                UniqueLock dst_guard(m_inactive.m_lock);
                m_inactive.EnqueueUnlocked(page);
            }
        }

        /// @brief Inactive → Active.  Re-reference path.
        void Reactivate(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Reactivate: null page");
            FK_BUG_ON(page->State() != PageState::Inactive,
                "PageQueueSet::Reactivate: page is not Inactive (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->State()));
            {
                UniqueLock src_guard(m_inactive.m_lock);
                m_inactive.RemoveUnlocked(page);
                page->TransitionTo(PageState::Active);
            }
            {
                UniqueLock dst_guard(m_active.m_lock);
                m_active.EnqueueUnlocked(page);
            }
        }

        /// @brief Any → Wired.  Pin path (kernel stacks, DMA, slab).
        void Wire(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Wire: null page");
            {
                PageQueue& src = QueueForState(page->State());
                UniqueLock src_guard(src.m_lock);
                src.RemoveUnlocked(page);
                page->TransitionTo(PageState::Wired);
            }
            {
                UniqueLock dst_guard(m_wired.m_lock);
                m_wired.EnqueueUnlocked(page);
            }
        }

        /// @brief Wired → Active.  Unpin path.
        void Unwire(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Unwire: null page");
            FK_BUG_ON(page->State() != PageState::Wired,
                "PageQueueSet::Unwire: page is not Wired (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->State()));
            {
                UniqueLock src_guard(m_wired.m_lock);
                m_wired.RemoveUnlocked(page);
                page->TransitionTo(PageState::Active);
            }
            {
                UniqueLock dst_guard(m_active.m_lock);
                m_active.EnqueueUnlocked(page);
            }
        }

        /// @brief Inactive → Laundry.  Dirty page selected for writeback.
        void LaunderPage(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::LaunderPage: null page");
            FK_BUG_ON(page->State() != PageState::Inactive,
                "PageQueueSet::LaunderPage: page is not Inactive (pfn={}, state={})",
                page->pfn.value, static_cast<u8>(page->State()));
            FK_BUG_ON(!page->TestFlag(PageFlags::Dirty),
                "PageQueueSet::LaunderPage: page is not dirty (pfn={}) — "
                "only dirty pages go to laundry",
                page->pfn.value);
            {
                UniqueLock src_guard(m_inactive.m_lock);
                m_inactive.RemoveUnlocked(page);
                page->TransitionTo(PageState::Laundry);
                page->SetFlag(PageFlags::Writeback);
            }
            {
                UniqueLock dst_guard(m_laundry.m_lock);
                m_laundry.EnqueueUnlocked(page);
            }
        }

        /// @brief Any → Free.  Deallocation / eviction path.
        void Free(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "PageQueueSet::Free: null page");
            FK_BUG_ON(page->TestFlag(PageFlags::Locked),
                "PageQueueSet::Free: cannot free a locked page (pfn={})",
                page->pfn.value);
            {
                PageQueue& src = QueueForState(page->State());
                UniqueLock src_guard(src.m_lock);
                src.RemoveUnlocked(page);
                page->TransitionTo(PageState::Free);
                page->ClearFlag(PageFlags::Dirty);
                page->ClearFlag(PageFlags::Referenced);
                page->ClearFlag(PageFlags::Writeback);
                page->ClearOwner();
            }
            {
                UniqueLock dst_guard(m_free.m_lock);
                m_free.EnqueueUnlocked(page);
            }
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
        ///        - Walk inactive queue head-to-tail under the inactive lock.
        ///        - If page has Referenced flag: clear it, move to tail (second chance).
        ///        - If page is clean and unreferenced: eviction candidate (removed;
        ///          caller transitions to Free after fully unmapping the page).
        ///        - If page is dirty and unreferenced: send to laundry.
        ///        - Stop when target is reached or queue exhausted.
        [[nodiscard]] usize ScanInactive(usize target,
                                          PageDescriptor** candidates,
                                          usize max_candidates) noexcept {
            FK_BUG_ON(candidates == nullptr && max_candidates > 0,
                "PageQueueSet::ScanInactive: null candidates array with non-zero max");

            const usize limit = target < max_candidates ? target : max_candidates;
            usize found    = 0;
            usize scanned  = 0;
            const usize queue_size = m_inactive.Count(); // Snapshot (relaxed read)

            // We scan at most queue_size entries to avoid infinite loops
            // (since we re-enqueue referenced pages at the tail).
            while (found < limit && scanned < queue_size) {
                PageDescriptor* page;
                {
                    UniqueLock guard(m_inactive.m_lock);
                    page = m_inactive.DequeueUnlocked();
                }
                if (!page) break;
                ++scanned;

                if (page->TestFlag(PageFlags::Locked)) {
                    // Locked pages cannot be evicted. Put back at tail.
                    // TransitionTo() would be a no-op (Inactive→Inactive), so
                    // we skip it — the state is already Inactive.
                    UniqueLock guard(m_inactive.m_lock);
                    m_inactive.EnqueueUnlocked(page);
                    continue;
                }

                if (page->TestFlag(PageFlags::Referenced)) {
                    page->ClearFlag(PageFlags::Referenced);
                    UniqueLock guard(m_inactive.m_lock);
                    m_inactive.EnqueueUnlocked(page);
                    continue;
                }

                if (page->TestFlag(PageFlags::Dirty)) {
                    {
                        // We already removed from inactive; no source lock needed.
                        page->TransitionTo(PageState::Laundry);
                        page->SetFlag(PageFlags::Writeback);
                    }
                    {
                        UniqueLock guard(m_laundry.m_lock);
                        m_laundry.EnqueueUnlocked(page);
                    }
                    continue;
                }

                // Clean, unreferenced, unlocked page — eviction candidate.
                // The page has been removed from the inactive queue.
                // Its state is still Inactive — the caller is responsible for
                // final state→Free transition after PTE teardown.
                candidates[found++] = page;
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
            usize demoted  = 0;
            usize scanned  = 0;
            const usize queue_size = m_active.Count();

            while (demoted < target && scanned < queue_size) {
                PageDescriptor* page;
                {
                    UniqueLock guard(m_active.m_lock);
                    page = m_active.DequeueUnlocked();
                }
                if (!page) break;
                ++scanned;

                if (page->TestFlag(PageFlags::Referenced)) {
                    page->ClearFlag(PageFlags::Referenced);
                    UniqueLock guard(m_active.m_lock);
                    m_active.EnqueueUnlocked(page);
                    continue;
                }

                page->TransitionTo(PageState::Inactive);
                {
                    UniqueLock guard(m_inactive.m_lock);
                    m_inactive.EnqueueUnlocked(page);
                }
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
        // Statistics (lock-free approximate reads)
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
