#pragma once

#include <FoundationKitMemory/Management/PageDescriptor.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PageDescriptorArray — dense PFN-indexed page descriptor table
    // =========================================================================

    /// @brief Global dense array of PageDescriptor, one per physical page frame.
    ///
    /// @desc  Analogous to Linux's `mem_map[]` / BSD's `vm_page_array`.
    ///        Indexed by PFN relative to a base PFN. The array storage is
    ///        provided externally (typically carved out of early-boot memory)
    ///        and populated via Initialize().
    ///
    ///        After Initialize(), the array is immutable in structure — only
    ///        the PageDescriptor contents change (state, flags, map_count, etc.).
    ///
    ///        All PFN lookups are bounds-checked with FK_BUG_ON.
    ///
    /// @tparam MaxPages  Static upper bound on the number of pages. Used only
    ///                   for compile-time sanity checks — the actual count is
    ///                   determined at boot time.
    template <usize MaxPages = (1ULL << 20)>  // Default: up to 4GB at 4K pages
    class PageDescriptorArray {
        static_assert(MaxPages >= 1, "PageDescriptorArray: MaxPages must be >= 1");
        static_assert(MaxPages <= (1ULL << 28),
            "PageDescriptorArray: MaxPages > 256M pages (1TB) is architecturally suspect");

    public:
        constexpr PageDescriptorArray() noexcept = default;

        PageDescriptorArray(const PageDescriptorArray&)            = delete;
        PageDescriptorArray& operator=(const PageDescriptorArray&) = delete;

        // ----------------------------------------------------------------
        // Initialization
        // ----------------------------------------------------------------

        /// @brief Initialize the descriptor array with externally provided storage.
        ///
        /// @param storage     Pre-allocated memory for the PageDescriptor array.
        ///                    Must be at least `page_count * sizeof(PageDescriptor)` bytes.
        ///                    Must be aligned to `alignof(PageDescriptor)`.
        /// @param base_pfn    PFN of the first page this array covers.
        /// @param page_count  Number of pages (descriptors) to manage.
        ///
        /// @desc  Each PageDescriptor is placement-constructed in Free state with
        ///        its PFN set. The storage is NOT zeroed — we explicitly construct.
        void Initialize(void* storage, Pfn base_pfn, usize page_count) noexcept {
            FK_BUG_ON(m_initialized,
                "PageDescriptorArray::Initialize: already initialized — double init is a boot bug");
            FK_BUG_ON(storage == nullptr,
                "PageDescriptorArray::Initialize: null storage");
            FK_BUG_ON(page_count == 0,
                "PageDescriptorArray::Initialize: zero page_count");
            FK_BUG_ON(page_count > MaxPages,
                "PageDescriptorArray::Initialize: page_count ({}) exceeds MaxPages ({})",
                page_count, MaxPages);
            FK_BUG_ON(!base_pfn.IsValid(),
                "PageDescriptorArray::Initialize: invalid base PFN");
            FK_BUG_ON((reinterpret_cast<uptr>(storage) % alignof(PageDescriptor)) != 0,
                "PageDescriptorArray::Initialize: storage is not aligned to {} bytes",
                alignof(PageDescriptor));

            m_descriptors = static_cast<PageDescriptor*>(storage);
            m_base_pfn    = base_pfn;
            m_count       = page_count;

            // Placement-construct each descriptor in Free state.
            for (usize i = 0; i < m_count; ++i) {
                PageDescriptor* desc = &m_descriptors[i];
                FoundationKitCxxStl::ConstructAt<PageDescriptor>(desc);
                desc->pfn   = Pfn{m_base_pfn.value + i};
                desc->state.Store(static_cast<u8>(PageState::Free), MemoryOrder::Release);
                desc->flags.Store(0, MemoryOrder::Release);
                desc->order = 0;
                desc->SetOwner(nullptr, 0);
            }

            m_initialized = true;
        }

        // ----------------------------------------------------------------
        // Lookup
        // ----------------------------------------------------------------

        /// @brief Get the PageDescriptor for a given PFN.
        /// @param pfn  Must be in [base_pfn, base_pfn + count).
        [[nodiscard]] PageDescriptor& Get(Pfn pfn) noexcept {
            FK_BUG_ON(!m_initialized,
                "PageDescriptorArray::Get: not initialized");
            FK_BUG_ON(!pfn.IsValid(),
                "PageDescriptorArray::Get: invalid PFN");
            FK_BUG_ON(pfn.value < m_base_pfn.value,
                "PageDescriptorArray::Get: PFN {} is below base PFN {}",
                pfn.value, m_base_pfn.value);

            const usize index = pfn.value - m_base_pfn.value;
            FK_BUG_ON(index >= m_count,
                "PageDescriptorArray::Get: PFN {} (index {}) is out of bounds (count={})",
                pfn.value, index, m_count);

            return m_descriptors[index];
        }

        /// @brief Const version of Get.
        [[nodiscard]] const PageDescriptor& Get(Pfn pfn) const noexcept {
            FK_BUG_ON(!m_initialized,
                "PageDescriptorArray::Get: not initialized");
            FK_BUG_ON(!pfn.IsValid(),
                "PageDescriptorArray::Get: invalid PFN");
            FK_BUG_ON(pfn.value < m_base_pfn.value,
                "PageDescriptorArray::Get: PFN {} is below base PFN {}",
                pfn.value, m_base_pfn.value);

            const usize index = pfn.value - m_base_pfn.value;
            FK_BUG_ON(index >= m_count,
                "PageDescriptorArray::Get: PFN {} (index {}) is out of bounds (count={})",
                pfn.value, index, m_count);

            return m_descriptors[index];
        }

        /// @brief Get the PageDescriptor for a given PhysicalAddress.
        [[nodiscard]] PageDescriptor& GetByPhysical(PhysicalAddress pa) noexcept {
            return Get(PhysicalToPfn(pa));
        }

        /// @brief Const version.
        [[nodiscard]] const PageDescriptor& GetByPhysical(PhysicalAddress pa) const noexcept {
            return Get(PhysicalToPfn(pa));
        }

        /// @brief Check if a PFN is within the managed range.
        [[nodiscard]] bool Contains(Pfn pfn) const noexcept {
            if (!m_initialized || !pfn.IsValid()) return false;
            if (pfn.value < m_base_pfn.value) return false;
            return (pfn.value - m_base_pfn.value) < m_count;
        }

        // ----------------------------------------------------------------
        // Folio operations
        // ----------------------------------------------------------------

        /// @brief Get a Folio view of the compound page starting at `pfn`.
        [[nodiscard]] Folio GetFolio(Pfn pfn) noexcept {
            PageDescriptor& desc = Get(pfn);
            FK_BUG_ON(desc.IsTail(),
                "PageDescriptorArray::GetFolio: PFN {} is a tail page — "
                "use the head PFN instead",
                pfn.value);
            return Folio{&desc};
        }

        /// @brief Initialize a compound Folio at the given PFN range.
        ///
        /// @param base_pfn  PFN of the first page.
        /// @param order     Compound order (0 = single, 1 = 2 pages, etc.)
        ///
        /// @desc  Sets Head on pages[0] and Compound on pages[1..n-1].
        void InitializeFolio(Pfn base_pfn, u8 order) noexcept {
            FK_BUG_ON(!m_initialized,
                "PageDescriptorArray::InitializeFolio: not initialized");

            const usize count = 1ULL << order;
            FK_BUG_ON(base_pfn.value < m_base_pfn.value,
                "PageDescriptorArray::InitializeFolio: PFN {} below base {}",
                base_pfn.value, m_base_pfn.value);
            FK_BUG_ON((base_pfn.value - m_base_pfn.value) + count > m_count,
                "PageDescriptorArray::InitializeFolio: PFN range [{}, {}) exceeds array bounds",
                base_pfn.value, base_pfn.value + count);

            PageDescriptor* pages = &Get(base_pfn);
            InitCompoundFolio(pages, order, base_pfn);
        }

        // ----------------------------------------------------------------
        // Iteration
        // ----------------------------------------------------------------

        /// @brief Invoke func(PageDescriptor&) for every descriptor.
        template <typename Func>
        void ForEach(Func&& func) noexcept {
            FK_BUG_ON(!m_initialized,
                "PageDescriptorArray::ForEach: not initialized");
            for (usize i = 0; i < m_count; ++i) {
                func(m_descriptors[i]);
            }
        }

        /// @brief Invoke func(const PageDescriptor&) for every descriptor.
        template <typename Func>
        void ForEach(Func&& func) const noexcept {
            FK_BUG_ON(!m_initialized,
                "PageDescriptorArray::ForEach: not initialized");
            for (usize i = 0; i < m_count; ++i) {
                func(m_descriptors[i]);
            }
        }

        // ----------------------------------------------------------------
        // Statistics
        // ----------------------------------------------------------------

        /// @brief Count pages in a specific state.
        [[nodiscard]] usize CountByState(PageState target) const noexcept {
            FK_BUG_ON(!m_initialized,
                "PageDescriptorArray::CountByState: not initialized");
            usize result = 0;
            for (usize i = 0; i < m_count; ++i) {
                if (m_descriptors[i].State() == target) ++result;
            }
            return result;
        }

        [[nodiscard]] usize TotalPages()    const noexcept { return m_count; }
        [[nodiscard]] Pfn   BasePfn()       const noexcept { return m_base_pfn; }
        [[nodiscard]] bool  IsInitialized() const noexcept { return m_initialized; }

        /// @brief Required storage size in bytes for `page_count` descriptors.
        [[nodiscard]] static constexpr usize StorageSize(usize page_count) noexcept {
            return page_count * sizeof(PageDescriptor);
        }

    private:
        PageDescriptor* m_descriptors = nullptr;
        Pfn             m_base_pfn    = {};
        usize           m_count       = 0;
        bool            m_initialized = false;
    };

} // namespace FoundationKitMemory
