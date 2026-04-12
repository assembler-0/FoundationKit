#pragma once

#include <FoundationKitMemory/Management/MemoryRegion.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // RegionType
    // ============================================================================

    enum class RegionType : u8 {
        Generic      = 0,
        DmaCoherent  = 1,
        PerCpuStack  = 2,
        EarlyBoot    = 3,
        Persistent   = 4,
        Mmio         = 5,
        Reserved     = 6,
    };

    // ============================================================================
    // RegionFlags
    // ============================================================================

    enum class RegionFlags : u8 {
        None       = 0,
        Readable   = 1 << 0,
        Writable   = 1 << 1,
        Executable = 1 << 2,
        Cacheable  = 1 << 3,
        Pinned     = 1 << 4,
        Zeroed     = 1 << 5,
        User       = 1 << 6,
    };

    [[nodiscard]] constexpr RegionFlags operator|(RegionFlags a, RegionFlags b) noexcept {
        return static_cast<RegionFlags>(static_cast<u8>(a) | static_cast<u8>(b));
    }
    [[nodiscard]] constexpr RegionFlags operator&(RegionFlags a, RegionFlags b) noexcept {
        return static_cast<RegionFlags>(static_cast<u8>(a) & static_cast<u8>(b));
    }
    [[nodiscard]] constexpr bool HasRegionFlag(RegionFlags flags, RegionFlags flag) noexcept {
        return (static_cast<u8>(flags) & static_cast<u8>(flag)) != 0;
    }

    // ============================================================================
    // RegionDescriptor
    // ============================================================================

    /// @brief Extended memory region with zone, NUMA, and ownership metadata.
    /// @desc  Used by PhysicalMemoryMap as the authoritative zone descriptor.
    ///        RegionDescriptorPool has been removed — use PhysicalMemoryMap instead.
    struct RegionDescriptor {
        static constexpr u64 kNoNumaNode = ~0ULL;
        static constexpr u64 kNoOwner   = 0ULL;

        MemoryRegion region;
        RegionType   type      = RegionType::Generic;
        RegionFlags  flags     = RegionFlags::None;
        u64          numa_node = kNoNumaNode;
        u64          owner_id  = kNoOwner;

        constexpr RegionDescriptor() noexcept = default;

        constexpr RegionDescriptor(
            void*       base,
            usize       size,
            RegionType  type_v  = RegionType::Generic,
            RegionFlags flags_v = RegionFlags::None,
            u64         numa_v  = kNoNumaNode,
            u64         owner_v = kNoOwner
        ) noexcept
            : region(base, size), type(type_v), flags(flags_v),
              numa_node(numa_v), owner_id(owner_v)
        {}

        constexpr RegionDescriptor(
            const MemoryRegion& r,
            RegionType          type_v  = RegionType::Generic,
            RegionFlags         flags_v = RegionFlags::None,
            u64                 numa_v  = kNoNumaNode,
            u64                 owner_v = kNoOwner
        ) noexcept
            : region(r), type(type_v), flags(flags_v),
              numa_node(numa_v), owner_id(owner_v)
        {}

        [[nodiscard]] constexpr byte*  Base()                    const noexcept { return region.Base(); }
        [[nodiscard]] constexpr byte*  End()                     const noexcept { return region.End();  }
        [[nodiscard]] constexpr usize  Size()                    const noexcept { return region.Size(); }
        [[nodiscard]] constexpr bool   IsValid()                 const noexcept { return region.IsValid(); }
        [[nodiscard]] constexpr bool   Contains(const void* ptr) const noexcept { return region.Contains(ptr); }
        [[nodiscard]] constexpr bool   Overlaps(const RegionDescriptor& o) const noexcept {
            return region.Overlaps(o.region);
        }

        [[nodiscard]] constexpr bool IsReadable()   const noexcept { return HasRegionFlag(flags, RegionFlags::Readable); }
        [[nodiscard]] constexpr bool IsWritable()   const noexcept { return HasRegionFlag(flags, RegionFlags::Writable); }
        [[nodiscard]] constexpr bool IsExecutable() const noexcept { return HasRegionFlag(flags, RegionFlags::Executable); }
        [[nodiscard]] constexpr bool IsCacheable()  const noexcept { return HasRegionFlag(flags, RegionFlags::Cacheable); }
        [[nodiscard]] constexpr bool IsPinned()     const noexcept { return HasRegionFlag(flags, RegionFlags::Pinned); }
        [[nodiscard]] constexpr bool IsZeroed()     const noexcept { return HasRegionFlag(flags, RegionFlags::Zeroed); }
        [[nodiscard]] constexpr bool IsNumaAware()  const noexcept { return numa_node != kNoNumaNode; }
        [[nodiscard]] constexpr bool IsOwned()      const noexcept { return owner_id  != kNoOwner; }
        [[nodiscard]] constexpr bool IsDmaCoherent() const noexcept { return type == RegionType::DmaCoherent; }
        [[nodiscard]] constexpr bool IsMmio()        const noexcept { return type == RegionType::Mmio; }

        [[nodiscard]] constexpr RegionDescriptor SubDescriptor(usize offset, usize sub_size) const noexcept {
            return {region.SubRegion(offset, sub_size), type, flags, numa_node, owner_id};
        }

        void Verify() const noexcept {
            region.Verify();
            FK_BUG_ON(type == RegionType::Mmio && HasRegionFlag(flags, RegionFlags::Cacheable),
                "RegionDescriptor: MMIO region must not be cacheable");
        }
    };

} // namespace FoundationKitMemory
