#pragma once

#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PageState — lifecycle state machine
    // =========================================================================

    /// @brief Physical page lifecycle state.
    ///
    /// @desc  Governs which queue a page belongs to. Legal transitions:
    ///
    ///        Free ──→ Active    (page fault, allocation)
    ///        Active ──→ Inactive (deactivation scan)
    ///        Active ──→ Wired   (Wire())
    ///        Inactive ──→ Active (re-reference)
    ///        Inactive ──→ Laundry (dirty page selected for writeback)
    ///        Inactive ──→ Free  (clean eviction)
    ///        Laundry ──→ Free   (writeback complete)
    ///        Wired ──→ Active   (Unwire())
    ///
    ///        Any other transition is a kernel bug.
    enum class PageState : u8 {
        Free     = 0,   ///< On the free queue, available for allocation.
        Active   = 1,   ///< Recently referenced, not an eviction candidate.
        Inactive = 2,   ///< Eviction candidate (LRU approximation).
        Wired    = 3,   ///< Pinned — kernel stacks, DMA, slab. Never evictable.
        Laundry  = 4,   ///< Dirty, selected for writeback. Will transition to Free.
    };

    // =========================================================================
    // PageFlags — per-page attribute bits
    // =========================================================================

    /// @brief Per-page flags. Stored atomically as a u16.
    enum class PageFlags : u16 {
        None       = 0,
        Dirty      = 1 << 0,   ///< Page contents differ from backing store.
        Referenced = 1 << 1,   ///< Accessed since last LRU scan (cleared by scanner).
        Locked     = 1 << 2,   ///< Page is locked for I/O (cannot be evicted or freed).
        Writeback  = 1 << 3,   ///< Currently being written back to backing store.
        Compound   = 1 << 4,   ///< This is a tail page in a compound Folio.
        Head       = 1 << 5,   ///< This is the head page of a compound Folio.
        Slab       = 1 << 6,   ///< Page is managed by the slab allocator.
        Reserved   = 1 << 7,   ///< Firmware/hardware reserved — never allocatable.
        Zero       = 1 << 8,   ///< Page is known to be zero-filled (optimization hint).
        SwapBacked = 1 << 9,   ///< Page has a swap slot allocated.
        Movable    = 1 << 10,  ///< Page can be migrated (for compaction).
    };

    [[nodiscard]] constexpr PageFlags operator|(PageFlags a, PageFlags b) noexcept {
        return static_cast<PageFlags>(static_cast<u16>(a) | static_cast<u16>(b));
    }
    [[nodiscard]] constexpr PageFlags operator&(PageFlags a, PageFlags b) noexcept {
        return static_cast<PageFlags>(static_cast<u16>(a) & static_cast<u16>(b));
    }
    [[nodiscard]] constexpr PageFlags operator~(PageFlags a) noexcept {
        return static_cast<PageFlags>(~static_cast<u16>(a));
    }
    [[nodiscard]] constexpr bool HasPageFlag(PageFlags flags, PageFlags flag) noexcept {
        return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0;
    }

    // Forward declarations
    class VmObject;

    // =========================================================================
    // PageDescriptor — per-physical-page metadata (struct page equivalent)
    // =========================================================================

    /// @brief Per-physical-page metadata indexed by PFN.
    ///
    ///        Layout is cache-line aware: hot fields (state, flags, lru) are
    ///        in the first 64 bytes; cold fields (owner, owner_offset) follow.
    struct PageDescriptor {
        // ----------------------------------------------------------------
        // Hot fields (first cache line)
        // ----------------------------------------------------------------
        Structure::IntrusiveDoublyLinkedListNode lru;   ///< LRU queue linkage (protected by PageQueue::m_lock).

        /// @brief Current lifecycle state — Sync::Atomic for SMP safety.
        Atomic<u8> state{static_cast<u8>(PageState::Free)};

        /// @brief Per-page attribute bits — Sync::Atomic for SMP safety.
        Atomic<u16> flags{0};

        u8         order  = 0;                          ///< Compound order (0 = single page, 1 = 2 pages, etc.)
        u8         _pad0  = 0;
        Pfn        pfn    = {};                         ///< Self-reference PFN (immutable after InitCompoundFolio).

        /// @brief Number of PTEs mapping this page. Atomic for concurrent map/unmap.
        ///        0 = unmapped, >0 = mapped. Used by rmap and eviction decisions.
        Atomic<u32> map_count{0};

        // ----------------------------------------------------------------
        // Cold fields (second cache line on most architectures)
        // ----------------------------------------------------------------

        /// @brief Owning VmObject — atomic to prevent torn-pointer observer bugs.
        Sync::Atomic<uptr>   a_owner{0};

        /// @brief Byte offset within the owning VmObject — atomic for the same reason.
        Sync::Atomic<u64>    a_owner_offset{0};

        // ----------------------------------------------------------------
        // State Transition API
        // ----------------------------------------------------------------

        /// @brief Validate and execute a state transition.
        /// @param new_state  Target state.
        void TransitionTo(PageState new_state) noexcept {
            const u8 raw_current = state.Load(Sync::MemoryOrder::Acquire);
            const auto current   = static_cast<PageState>(raw_current);

            FK_BUG_ON(current == new_state,
                "PageDescriptor::TransitionTo: self-transition to {} is a no-op bug (pfn={})",
                static_cast<u8>(new_state), pfn.value);

            switch (current) {
                case PageState::Free:
                    FK_BUG_ON(new_state != PageState::Active && new_state != PageState::Wired,
                        "PageDescriptor::TransitionTo: illegal Free→{} (pfn={}) — "
                        "Free pages can only transition to Active or Wired",
                        static_cast<u8>(new_state), pfn.value);
                    break;

                case PageState::Active:
                    FK_BUG_ON(new_state != PageState::Inactive &&
                              new_state != PageState::Wired &&
                              new_state != PageState::Free,
                        "PageDescriptor::TransitionTo: illegal Active→{} (pfn={}) — "
                        "Active pages can transition to Inactive, Wired, or Free",
                        static_cast<u8>(new_state), pfn.value);
                    break;

                case PageState::Inactive:
                    FK_BUG_ON(new_state != PageState::Active &&
                              new_state != PageState::Laundry &&
                              new_state != PageState::Free,
                        "PageDescriptor::TransitionTo: illegal Inactive→{} (pfn={}) — "
                        "Inactive pages can transition to Active, Laundry, or Free",
                        static_cast<u8>(new_state), pfn.value);
                    break;

                case PageState::Wired:
                    FK_BUG_ON(new_state != PageState::Active && new_state != PageState::Free,
                        "PageDescriptor::TransitionTo: illegal Wired→{} (pfn={}) — "
                        "Wired pages can only transition to Active or Free",
                        static_cast<u8>(new_state), pfn.value);
                    break;

                case PageState::Laundry:
                    FK_BUG_ON(new_state != PageState::Free && new_state != PageState::Inactive,
                        "PageDescriptor::TransitionTo: illegal Laundry→{} (pfn={}) — "
                        "Laundry pages can only transition to Free or Inactive (writeback cancelled)",
                        static_cast<u8>(new_state), pfn.value);
                    break;
            }

            // AcqRel: we have acquired the current state above; now we release
            // the new state to all observers.  Any CPU reading state with Acquire
            // will see all stores that happened before this point.
            state.Store(static_cast<u8>(new_state), Sync::MemoryOrder::Release);
        }

        // ----------------------------------------------------------------
        // State accessor
        // ----------------------------------------------------------------

        /// @brief Read the current state with Acquire ordering.
        [[nodiscard]] PageState State() const noexcept {
            return static_cast<PageState>(state.Load(Sync::MemoryOrder::Acquire));
        }

        /// @brief Atomically set a flag bit.
        void SetFlag(PageFlags flag) noexcept {
            // FetchOr: LOCK OR on x86-64.  AcqRel ensures the flag is
            // visible to any CPU that reads flags with Acquire ordering.
            flags.FetchOr(static_cast<u16>(flag), Sync::MemoryOrder::AcqRel);
        }

        /// @brief Atomically clear a flag bit.
        void ClearFlag(PageFlags flag) noexcept {
            // FetchAnd: LOCK AND on x86-64.
            flags.FetchAnd(static_cast<u16>(~static_cast<u16>(flag)), Sync::MemoryOrder::AcqRel);
        }

        [[nodiscard]] bool TestFlag(PageFlags flag) const noexcept {
            return HasPageFlag(
                static_cast<PageFlags>(flags.Load(Sync::MemoryOrder::Acquire)),
                flag);
        }

        [[nodiscard]] PageFlags Flags() const noexcept {
            return static_cast<PageFlags>(flags.Load(Sync::MemoryOrder::Acquire));
        }

        // ----------------------------------------------------------------
        // Map count manipulation
        // ----------------------------------------------------------------

        /// @brief Increment the PTE map count. Called when a new PTE maps this page.
        void MapCountInc() noexcept {
            map_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Decrement the PTE map count. Called when a PTE is removed.
        ///        FK_BUG if count was already zero (double-unmap).
        void MapCountDec() noexcept {
            const u32 prev = map_count.FetchSub(1, Sync::MemoryOrder::AcqRel);
            FK_BUG_ON(prev == 0,
                "PageDescriptor::MapCountDec: map_count was already zero (pfn={}) — "
                "double-unmap or PTE tracking corruption",
                pfn.value);
        }

        /// @brief True if no PTEs currently map this page.
        [[nodiscard]] bool IsUnmapped() const noexcept {
            return map_count.Load(Sync::MemoryOrder::Relaxed) == 0;
        }

        /// @brief Atomically bind an owner VmObject and offset.
        ///
        /// @desc  Uses Release ordering so that any CPU reading owner with
        ///        Acquire sees a consistent (non-torn) pointer AND the
        ///        freshly-written owner_offset in the correct order.
        void SetOwner(VmObject* new_owner, u64 offset) noexcept {
            // Write offset first (Release) then the pointer (Release).
            // A reader that loads the pointer with Acquire will then see
            // the offset as well — no torn-pair is possible.
            a_owner_offset.Store(offset, Sync::MemoryOrder::Release);
            a_owner.Store(reinterpret_cast<uptr>(new_owner), Sync::MemoryOrder::Release);
        }

        /// @brief Atomically clear the owner binding.
        void ClearOwner() noexcept {
            // AcqRel: acquire current state, release zero to observers.
            a_owner.Store(0, Sync::MemoryOrder::Release);
            a_owner_offset.Store(0, Sync::MemoryOrder::Release);
        }

        [[nodiscard]] VmObject* Owner() const noexcept {
            return reinterpret_cast<VmObject*>(a_owner.Load(Sync::MemoryOrder::Acquire));
        }

        [[nodiscard]] u64 OwnerOffset() const noexcept {
            return a_owner_offset.Load(Sync::MemoryOrder::Acquire);
        }

        // ----------------------------------------------------------------
        // Compound page queries
        // ----------------------------------------------------------------

        /// @brief True if this descriptor is the head of a compound Folio.
        [[nodiscard]] bool IsHead() const noexcept { return TestFlag(PageFlags::Head); }

        /// @brief True if this descriptor is a tail page of a compound Folio.
        [[nodiscard]] bool IsTail() const noexcept { return TestFlag(PageFlags::Compound); }

        /// @brief True if this is a single (order-0) page — neither head nor tail.
        [[nodiscard]] bool IsSingle() const noexcept { return order == 0 && !IsHead() && !IsTail(); }

        /// @brief Number of base pages in a compound page of this order.
        [[nodiscard]] constexpr usize PagesInOrder() const noexcept { return 1ULL << order; }

        /// @brief Total bytes covered by this page/compound.
        [[nodiscard]] constexpr usize SizeBytes() const noexcept { return PagesInOrder() * kPageSize; }

        // ----------------------------------------------------------------
        // Validation
        // ----------------------------------------------------------------

        /// @brief Full sanity check. Call from diagnostic/debug paths.
        void Verify() const noexcept {
            const PageState s = State();
            FK_BUG_ON(!pfn.IsValid(),
                "PageDescriptor::Verify: invalid PFN");
            FK_BUG_ON(IsTail() && IsHead(),
                "PageDescriptor::Verify: page is both Head and Tail (pfn={})", pfn.value);
            FK_BUG_ON(s == PageState::Free && !IsUnmapped(),
                "PageDescriptor::Verify: Free page has non-zero map_count (pfn={}, map_count={})",
                pfn.value, map_count.Load(Sync::MemoryOrder::Relaxed));
            FK_BUG_ON(s == PageState::Free && TestFlag(PageFlags::Dirty),
                "PageDescriptor::Verify: Free page has Dirty flag (pfn={})", pfn.value);
            FK_BUG_ON(s == PageState::Free && TestFlag(PageFlags::Locked),
                "PageDescriptor::Verify: Free page has Locked flag (pfn={})", pfn.value);
            FK_BUG_ON(TestFlag(PageFlags::Writeback) && s != PageState::Laundry,
                "PageDescriptor::Verify: Writeback flag set but state is not Laundry (pfn={}, state={})",
                pfn.value, static_cast<u8>(s));
        }
    };

    // =========================================================================
    // Folio — compound page abstraction
    // =========================================================================

    /// @brief A Folio represents a contiguous, power-of-two group of physical pages
    ///        managed as a single unit.
    ///
    /// @desc  Inspired by Linux kernel's struct folio. A Folio wraps the head
    ///        PageDescriptor of a compound page group. All operations (LRU,
    ///        dirty tracking, map count) are performed on the Folio level,
    ///        not on individual tail pages.
    ///
    ///        Folio is a non-owning view — it holds a raw pointer to the head
    ///        PageDescriptor. The PageDescriptorArray owns the actual storage.
    ///
    ///        Order 0 = 1 page  = 4K   (base page, not compound)
    ///        Order 1 = 2 pages = 8K
    ///        Order 4 = 16 pages = 64K (common large folio)
    ///        Order 9 = 512 pages = 2M (huge page on x86)
    ///        Order 18 = 262144 pages = 1G (gigantic page on x86)
    struct Folio {
        PageDescriptor* head = nullptr;   ///< Head page of the compound group.

        constexpr Folio() noexcept = default;
        explicit constexpr Folio(PageDescriptor* h) noexcept : head(h) {}

        /// @brief Create a Folio from a single order-0 page.
        [[nodiscard]] static Folio FromSinglePage(PageDescriptor* page) noexcept {
            FK_BUG_ON(page == nullptr, "Folio::FromSinglePage: null page");
            FK_BUG_ON(page->IsTail(),
                "Folio::FromSinglePage: cannot create Folio from a tail page (pfn={})",
                page->pfn.value);
            return Folio{page};
        }

        // ----------------------------------------------------------------
        // Properties
        // ----------------------------------------------------------------

        /// @brief Compound order. 0 for single pages.
        [[nodiscard]] u8 Order() const noexcept {
            FK_BUG_ON(!head, "Folio::Order: null head");
            return head->order;
        }

        /// @brief Number of base (4K) pages in this Folio.
        [[nodiscard]] usize PageCount() const noexcept {
            FK_BUG_ON(!head, "Folio::PageCount: null head");
            return head->PagesInOrder();
        }

        /// @brief Total size in bytes.
        [[nodiscard]] usize SizeBytes() const noexcept {
            FK_BUG_ON(!head, "Folio::SizeBytes: null head");
            return head->SizeBytes();
        }

        /// @brief PFN of the head page.
        [[nodiscard]] Pfn HeadPfn() const noexcept {
            FK_BUG_ON(!head, "Folio::HeadPfn: null head");
            return head->pfn;
        }

        /// @brief Physical address of the first byte.
        [[nodiscard]] PhysicalAddress PhysAddr() const noexcept {
            return PfnToPhysical(HeadPfn());
        }

        // ----------------------------------------------------------------
        // State access (delegated to head page)
        // ----------------------------------------------------------------

        [[nodiscard]] PageState State() const noexcept {
            FK_BUG_ON(!head, "Folio::State: null head");
            return head->State();
        }

        [[nodiscard]] PageFlags Flags() const noexcept {
            FK_BUG_ON(!head, "Folio::Flags: null head");
            return head->Flags();
        }

        void SetFlag(PageFlags flag) const noexcept {
            FK_BUG_ON(!head, "Folio::SetFlag: null head");
            head->SetFlag(flag);
        }

        void ClearFlag(PageFlags flag) const noexcept {
            FK_BUG_ON(!head, "Folio::ClearFlag: null head");
            head->ClearFlag(flag);
        }

        [[nodiscard]] bool TestFlag(PageFlags flag) const noexcept {
            FK_BUG_ON(!head, "Folio::TestFlag: null head");
            return head->TestFlag(flag);
        }

        // ----------------------------------------------------------------
        // Dirty tracking (Folio-level)
        // ----------------------------------------------------------------

        void MarkDirty() const noexcept { SetFlag(PageFlags::Dirty); }
        void ClearDirty() const noexcept { ClearFlag(PageFlags::Dirty); }
        [[nodiscard]] bool IsDirty() const noexcept { return TestFlag(PageFlags::Dirty); }

        // ----------------------------------------------------------------
        // Reference tracking (Folio-level map count)
        // ----------------------------------------------------------------

        void MapCountInc() const noexcept {
            FK_BUG_ON(!head, "Folio::MapCountInc: null head");
            head->MapCountInc();
        }

        void MapCountDec() const noexcept {
            FK_BUG_ON(!head, "Folio::MapCountDec: null head");
            head->MapCountDec();
        }

        [[nodiscard]] u32 MapCount() const noexcept {
            FK_BUG_ON(!head, "Folio::MapCount: null head");
            return head->map_count.Load(Sync::MemoryOrder::Relaxed);
        }

        [[nodiscard]] bool IsUnmapped() const noexcept {
            FK_BUG_ON(!head, "Folio::IsUnmapped: null head");
            return head->IsUnmapped();
        }

        // ----------------------------------------------------------------
        // Owner binding (for VmObject reverse mapping)
        // ----------------------------------------------------------------

        /// @brief Atomically bind an owner to the head descriptor.
        void SetOwner(VmObject* owner, u64 offset) const noexcept {
            FK_BUG_ON(!head, "Folio::SetOwner: null head");
            head->SetOwner(owner, offset);
        }

        void ClearOwner() const noexcept {
            FK_BUG_ON(!head, "Folio::ClearOwner: null head");
            head->ClearOwner();
        }

        [[nodiscard]] VmObject* Owner() const noexcept {
            FK_BUG_ON(!head, "Folio::Owner: null head");
            return head->Owner();
        }

        [[nodiscard]] u64 OwnerOffset() const noexcept {
            FK_BUG_ON(!head, "Folio::OwnerOffset: null head");
            return head->OwnerOffset();
        }

        // ----------------------------------------------------------------
        // LRU linkage access (for PageQueue)
        // ----------------------------------------------------------------

        [[nodiscard]] Structure::IntrusiveDoublyLinkedListNode* LruNode() const noexcept {
            FK_BUG_ON(!head, "Folio::LruNode: null head");
            return &head->lru;
        }

        // ----------------------------------------------------------------
        // Validity
        // ----------------------------------------------------------------

        [[nodiscard]] explicit operator bool() const noexcept { return head != nullptr; }

        [[nodiscard]] bool IsValid() const noexcept {
            return head != nullptr && head->pfn.IsValid();
        }

        // ----------------------------------------------------------------
        // Equality (by head PFN)
        // ----------------------------------------------------------------

        [[nodiscard]] bool operator==(const Folio& other) const noexcept {
            if (!head && !other.head) return true;
            if (!head || !other.head) return false;
            return head->pfn.value == other.head->pfn.value;
        }

        [[nodiscard]] bool operator!=(const Folio& other) const noexcept {
            return !(*this == other);
        }
    };

    // =========================================================================
    // Compound Folio Initialization
    // =========================================================================

    /// @brief Initialize a contiguous range of PageDescriptors as a compound Folio.
    ///
    /// @param pages    Pointer to the first PageDescriptor in the contiguous range.
    /// @param order    Compound order (0 = single page, 1 = 2 pages, etc.)
    /// @param base_pfn PFN of the first page.
    ///
    /// @desc  Sets the Head flag on pages[0] and Compound (tail) flag on pages[1..n-1].
    ///        All pages get the same order value. Only call this during PFA/boot init.
    inline void InitCompoundFolio(PageDescriptor* pages, u8 order, Pfn base_pfn) noexcept {
        FK_BUG_ON(pages == nullptr, "InitCompoundFolio: null pages array");
        FK_BUG_ON(order > 20, "InitCompoundFolio: order {} is unreasonably large", order);

        const usize count = 1ULL << order;

        // Head page — use direct atomic stores (we are the sole writer here,
        // Release ordering so the rest of the kernel sees a consistent page).
        pages[0].pfn   = base_pfn;
        pages[0].order = order;
        pages[0].state.Store(static_cast<u8>(PageState::Free), Sync::MemoryOrder::Release);
        pages[0].flags.Store(
            static_cast<u16>((order > 0) ? PageFlags::Head : PageFlags::None),
            Sync::MemoryOrder::Release);
        pages[0].a_owner.Store(0, Sync::MemoryOrder::Relaxed);
        pages[0].a_owner_offset.Store(0, Sync::MemoryOrder::Relaxed);

        // Tail pages
        for (usize i = 1; i < count; ++i) {
            pages[i].pfn   = Pfn{base_pfn.value + i};
            pages[i].order = order;
            pages[i].state.Store(static_cast<u8>(PageState::Free), Sync::MemoryOrder::Release);
            pages[i].flags.Store(static_cast<u16>(PageFlags::Compound), Sync::MemoryOrder::Release);
            pages[i].a_owner.Store(0, Sync::MemoryOrder::Relaxed);
            pages[i].a_owner_offset.Store(0, Sync::MemoryOrder::Relaxed);
        }
    }

} // namespace FoundationKitMemory
