#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // ResourceType — what kind of hardware resource this descriptor represents
    // =========================================================================

    enum class ResourceType : u8 {
        Mmio       = 0,   ///< Memory-mapped I/O region (BAR, DT reg)
        Irq        = 1,   ///< Interrupt line (logical IRQ number)
        DmaChannel = 2,   ///< DMA channel assignment
        BusIo      = 3,   ///< Port I/O (x86 in/out)
        BusMem     = 4,   ///< Bus memory window (PCI prefetchable BAR, etc.)
    };

    /// @brief Human-readable name for a ResourceType.
    [[nodiscard]] inline const char* ResourceTypeName(ResourceType t) noexcept {
        switch (t) {
            case ResourceType::Mmio:       return "MMIO";
            case ResourceType::Irq:        return "IRQ";
            case ResourceType::DmaChannel: return "DmaChannel";
            case ResourceType::BusIo:      return "BusIO";
            case ResourceType::BusMem:     return "BusMem";
        }
        return "<invalid>";
    }

    // =========================================================================
    // ResourceFlags — per-resource attribute bits
    // =========================================================================

    enum class ResourceFlags : u32 {
        None         = 0,
        Cacheable    = (1u << 0),   ///< Region is cacheable (write-back).
        Prefetchable = (1u << 1),   ///< PCI prefetchable BAR.
        Shareable    = (1u << 2),   ///< IRQ may be shared between devices.
        ReadOnly     = (1u << 3),   ///< MMIO region is read-only.
        Mapped       = (1u << 4),   ///< Region has been mapped into virtual memory.
        Active       = (1u << 5),   ///< Resource is actively claimed by a driver.
        EdgeTriggered= (1u << 6),   ///< IRQ is edge-triggered (vs level).
        WakeCapable  = (1u << 7),   ///< IRQ can wake from sleep.
    };

    // =========================================================================
    // ResourceDescriptor
    // =========================================================================

    /// @brief Describes a single hardware resource attached to a device.
    ///
    /// @desc  A device may have up to kMaxDeviceResources resources. Each one
    ///        describes a physical address range (MMIO/BusMem), an IRQ line,
    ///        a DMA channel, or a port I/O range.
    ///
    ///        For MMIO: base = physical address, size = byte count.
    ///        For IRQ:  base = logical IRQ number, size = 1.
    ///        For DMA:  base = channel number, size = 1.
    ///        For BusIo: base = port base, size = port count.
    struct ResourceDescriptor {
        ResourceType type  = ResourceType::Mmio;
        u8           index = 0;       ///< Resource index within the device (BAR0=0, BAR1=1, etc.)
        u32          flags = 0;       ///< Bitmask of ResourceFlags values.
        u64          base  = 0;       ///< Physical base address / IRQ number / channel ID.
        u64          size  = 0;       ///< Byte range (or 1 for IRQ/DMA).

        // --- Query helpers ---

        [[nodiscard]] constexpr bool HasFlag(ResourceFlags f) const noexcept {
            return (flags & static_cast<u32>(f)) != 0;
        }

        constexpr void SetFlag(ResourceFlags f) noexcept {
            flags |= static_cast<u32>(f);
        }

        constexpr void ClearFlag(ResourceFlags f) noexcept {
            flags &= ~static_cast<u32>(f);
        }

        /// @brief End address (base + size). Valid for MMIO and BusMem.
        [[nodiscard]] constexpr u64 End() const noexcept {
            FK_BUG_ON(size == 0, "ResourceDescriptor::End: size is zero");
            return base + size;
        }

        /// @brief Check if a physical address falls within this resource range.
        [[nodiscard]] constexpr bool Contains(u64 addr) const noexcept {
            return addr >= base && addr < (base + size);
        }
    };

    /// @brief Maximum number of resources a single DeviceNode can hold.
    static constexpr usize kMaxDeviceResources = 16;

    /// @brief Inline array of resources owned by a DeviceNode.
    ///
    /// @desc  Fixed-capacity, stack-allocated. No heap, no dynamic growth.
    ///        FK_BUG_ON on overflow — if a device has more than 16 resources,
    ///        bump the constant; do not silently drop.
    struct ResourceTable {
        ResourceDescriptor entries[kMaxDeviceResources]{};
        u8 count = 0;

        /// @brief Append a resource descriptor.
        void Add(const ResourceDescriptor& desc) noexcept {
            FK_BUG_ON(count >= kMaxDeviceResources,
                "ResourceTable::Add: overflow — device already has {} resources "
                "(max {}). Bump kMaxDeviceResources if legitimate.",
                count, kMaxDeviceResources);
            entries[count] = desc;
            ++count;
        }

        /// @brief Find the first resource of a given type.
        [[nodiscard]] const ResourceDescriptor* FindByType(ResourceType t) const noexcept {
            for (u8 i = 0; i < count; ++i) {
                if (entries[i].type == t) return &entries[i];
            }
            return nullptr;
        }

        /// @brief Find a resource by type and index (e.g., BAR2 = {Mmio, 2}).
        [[nodiscard]] const ResourceDescriptor* FindByTypeAndIndex(
                ResourceType t, u8 idx) const noexcept {
            for (u8 i = 0; i < count; ++i) {
                if (entries[i].type == t && entries[i].index == idx) return &entries[i];
            }
            return nullptr;
        }

        /// @brief Count resources of a given type.
        [[nodiscard]] u8 CountByType(ResourceType t) const noexcept {
            u8 n = 0;
            for (u8 i = 0; i < count; ++i) {
                if (entries[i].type == t) ++n;
            }
            return n;
        }
    };

} // namespace FoundationKitDevice

namespace FoundationKitCxxStl {
    template <>
    struct Formatter<FoundationKitDevice::ResourceType> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitDevice::ResourceType& t, const FormatSpec& = {}) {
            const char* name = FoundationKitDevice::ResourceTypeName(t);
            usize len = 0;
            while (name[len]) ++len;
            sb.Append(name, len);
        }
    };
} // namespace FoundationKitCxxStl
