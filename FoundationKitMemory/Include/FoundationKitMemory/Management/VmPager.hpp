#pragma once

#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitMemory/Management/PageDescriptor.hpp>
#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PagerType — identifies the backing store kind
    // =========================================================================

    /// @brief Identifies the kind of backing store a pager provides.
    enum class PagerType : u8 {
        Anonymous = 0,    ///< Zero-fill on first fault. Swap-backed (future).
        Device    = 1,    ///< MMIO identity-mapped, non-pageable.
        File      = 2,    ///< File-backed (reserved for future).
    };

    // =========================================================================
    // VmPagerBase — type-erased pager interface (RTTI-free)
    // =========================================================================

    /// @brief Type-erased backing store interface for VmObjects.
    ///
    /// @desc  Provides the "where do pages come from?" abstraction. When a page
    ///        fault occurs and the VmObject has no resident page at the faulting
    ///        offset, the VmObject's pager is asked to provide one via GetPage().
    ///
    ///        When a page is evicted (clean eviction or writeback complete), the
    ///        pager is notified via PutPage() so it can release resources.
    ///
    ///        This uses function pointers instead of virtual dispatch to remain
    ///        compliant with freestanding constraints (no vptr dependency).
    ///
    ///        Concrete pagers (AnonymousPager, DevicePager) provide static
    ///        functions that are stored here at construction time.
    struct VmPagerBase {
        /// @brief Get a page for the given offset in the VmObject.
        ///
        /// @param self    The pager instance (this pointer via type-erasure).
        /// @param offset  Byte offset within the VmObject.
        /// @param out_pa  [out] Physical address of the provided page.
        /// @param out_zeroed [out] True if the page is already zeroed.
        ///
        /// @return MemoryError::None on success.
        using GetPageFn = Expected<PhysicalAddress, MemoryError>(*)(
            VmPagerBase* self, u64 offset) noexcept;

        /// @brief Release a page back to the pager.
        ///
        /// @param self   The pager instance.
        /// @param offset Byte offset within the VmObject.
        /// @param pa     Physical address of the page being released.
        using PutPageFn = void(*)(VmPagerBase* self, u64 offset, PhysicalAddress pa) noexcept;

        /// @brief Query whether this pager supports page-out (eviction).
        using CanPageOutFn = bool(*)(const VmPagerBase* self) noexcept;

        GetPageFn    get_page;
        PutPageFn    put_page;
        CanPageOutFn can_page_out;
        PagerType    type;

        constexpr VmPagerBase(
            GetPageFn    get,
            PutPageFn    put,
            CanPageOutFn can,
            PagerType    t
        ) noexcept
            : get_page(get), put_page(put), can_page_out(can), type(t)
        {
            FK_BUG_ON(get == nullptr,
                "VmPagerBase: null get_page function pointer");
            FK_BUG_ON(put == nullptr,
                "VmPagerBase: null put_page function pointer");
            FK_BUG_ON(can == nullptr,
                "VmPagerBase: null can_page_out function pointer");
        }

        // Non-copyable — pagers are referenced, not copied.
        VmPagerBase(const VmPagerBase&) = delete;
        VmPagerBase& operator=(const VmPagerBase&) = delete;

        // ----------------------------------------------------------------
        // Convenience wrappers
        // ----------------------------------------------------------------

        [[nodiscard]] Expected<PhysicalAddress, MemoryError>
        GetPage(u64 offset) noexcept {
            return get_page(this, offset);
        }

        void PutPage(u64 offset, PhysicalAddress pa) noexcept {
            put_page(this, offset, pa);
        }

        [[nodiscard]] bool CanPageOut() const noexcept {
            return can_page_out(this);
        }

        [[nodiscard]] PagerType Type() const noexcept { return type; }
    };

    // =========================================================================
    // IVmPager concept — for compile-time pager validation
    // =========================================================================

    /// @brief Concept: T can serve as a VM pager.
    template <typename T>
    concept IVmPager = requires(T& pager, u64 offset, PhysicalAddress pa) {
        { pager.GetPage(offset) } -> SameAs<Expected<PhysicalAddress, MemoryError>>;
        { pager.PutPage(offset, pa) } -> SameAs<void>;
        { pager.CanPageOut() } -> SameAs<bool>;
        { pager.Type() } -> SameAs<PagerType>;
    };

    // =========================================================================
    // AnonymousPager — zero-fill pager for anonymous memory
    // =========================================================================

    /// @brief Pager for anonymous (non-file-backed) memory.
    ///
    /// @desc  On GetPage():
    ///        - Allocates a physical page from the page frame allocator.
    ///        - The page is NOT zeroed here — the fault handler zeros it via
    ///          a temporary mapping. We return the raw PA.
    ///
    ///        On PutPage():
    ///        - Frees the physical page back to the PFA.
    ///
    ///        CanPageOut() returns false until a swap backend is implemented.
    ///
    /// @tparam PageFrameAlloc  Must satisfy IPageFrameAllocator.
    template <typename PageFrameAlloc>
    struct AnonymousPager final : VmPagerBase {
        PageFrameAlloc& pfa;

        explicit AnonymousPager(PageFrameAlloc& allocator) noexcept
            : VmPagerBase(&SGetPage, &SPutPage, &SCanPageOut, PagerType::Anonymous)
            , pfa(allocator)
        {}

    private:
        static Expected<PhysicalAddress, MemoryError>
        SGetPage(VmPagerBase* self, u64 /*offset*/) noexcept {
            auto* me = static_cast<AnonymousPager*>(self);

            auto pfn_result = me->pfa.AllocatePages(1, RegionType::Generic);
            if (!pfn_result) return Unexpected(pfn_result.Error());

            return PfnToPhysical(pfn_result.Value());
        }

        static void SPutPage(VmPagerBase* self, u64 /*offset*/, PhysicalAddress pa) noexcept {
            auto* me = static_cast<AnonymousPager*>(self);
            FK_BUG_ON(pa.IsNull(),
                "AnonymousPager::PutPage: null physical address");

            me->pfa.FreePages(PhysicalToPfn(pa), 1);
        }

        static bool SCanPageOut(const VmPagerBase* /*self*/) noexcept {
            // No swap backend yet — cannot page out anonymous memory.
            return false;
        }
    };

    // =========================================================================
    // DevicePager — MMIO identity-mapped pager
    // =========================================================================

    /// @brief Pager for device (MMIO) memory regions.
    ///
    /// @desc  Device memory is never allocated or freed by the VMM — it's
    ///        owned by hardware. GetPage() returns the fixed physical address
    ///        computed from the device base + offset. PutPage() is a no-op.
    ///        CanPageOut() is always false.
    ///
    ///        The device_base is the physical address of the start of the
    ///        MMIO region. GetPage(offset) returns device_base + offset.
    struct DevicePager final : VmPagerBase {
        PhysicalAddress device_base;
        usize           device_size;

        DevicePager(PhysicalAddress base, usize size) noexcept
            : VmPagerBase(&SGetPage, &SPutPage, &SCanPageOut, PagerType::Device)
            , device_base(base)
            , device_size(size)
        {
            FK_BUG_ON(base.IsNull(),
                "DevicePager: null device base address");
            FK_BUG_ON(size == 0,
                "DevicePager: zero device size");
            FK_BUG_ON(!IsPageAligned(base.value),
                "DevicePager: device base {:#x} is not page-aligned", base.value);
            FK_BUG_ON(!IsPageAligned(size),
                "DevicePager: device size {} is not page-aligned", size);
        }

    private:
        static Expected<PhysicalAddress, MemoryError>
        SGetPage(VmPagerBase* self, u64 offset) noexcept {
            auto* me = static_cast<DevicePager*>(self);
            FK_BUG_ON(offset >= me->device_size,
                "DevicePager::GetPage: offset ({:#x}) exceeds device size ({:#x})",
                offset, me->device_size);
            FK_BUG_ON(!IsPageAligned(offset),
                "DevicePager::GetPage: offset {:#x} is not page-aligned", offset);

            return PhysicalAddress{me->device_base.value + offset};
        }

        static void SPutPage(VmPagerBase* /*self*/, u64 /*offset*/,
                              PhysicalAddress /*pa*/) noexcept {
            // Device memory is hardware-owned — PutPage is a no-op.
        }

        static bool SCanPageOut(const VmPagerBase* /*self*/) noexcept {
            return false;  // MMIO is never pageable.
        }
    };

    // =========================================================================
    // Static assertions
    // =========================================================================

    static_assert(IVmPager<VmPagerBase>,
        "VmPagerBase must satisfy IVmPager concept");

} // namespace FoundationKitMemory
