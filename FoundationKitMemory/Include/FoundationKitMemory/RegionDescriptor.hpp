#pragma once

#include <FoundationKitMemory/MemoryRegion.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // RegionType — Physical memory zone classification
    // ============================================================================

    /// @brief Physical zone classification for a memory region.
    enum class RegionType : u8 {
        Generic      = 0, ///< General-purpose RAM; no special constraints.
        DmaCoherent  = 1, ///< DMA-coherent; device can access without cache invalidation.
        PerCpuStack  = 2, ///< Per-CPU kernel stack memory.
        EarlyBoot    = 3, ///< Early boot arena (may be reclaimed after init).
        Persistent   = 4, ///< Survives reboots (NVRAM / persistent RAM).
        Mmio         = 5, ///< Memory-mapped I/O range; must not be cached.
        Reserved     = 6, ///< Firmware/ACPI-reserved; do not allocate.
    };

    // ============================================================================
    // RegionFlags — Attribute bitmask
    // ============================================================================

    enum class RegionFlags : u8 {
        None       = 0,
        Readable   = 1 << 0, ///< CPU read access permitted.
        Writable   = 1 << 1, ///< CPU write access permitted.
        Executable = 1 << 2, ///< Instruction fetch permitted.
        Cacheable  = 1 << 3, ///< Mapped with caching enabled.
        Pinned     = 1 << 4, ///< Must never be reclaimed under memory pressure.
        Zeroed     = 1 << 5, ///< Guaranteed to be zeroed on first access.
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

    /// @brief Extended memory region with NUMA, DMA, and ownership metadata.
    /// @desc Composes a `MemoryRegion` with rich zone metadata. Does NOT inherit
    ///       from `MemoryRegion` — composition avoids layout coupling.
    ///
    ///       `numa_node = kNoNumaNode` means the region is not NUMA-partitioned
    ///       or the platform does not support NUMA.
    struct RegionDescriptor {
        static constexpr u64 kNoNumaNode   = ~0ULL; ///< Sentinel: not NUMA-aware.
        static constexpr u64 kNoOwner      = 0ULL;  ///< Sentinel: no subsystem owner.

        MemoryRegion region;     ///< Bounded memory block (base + size + magic).
        RegionType   type;       ///< Physical zone classification.
        RegionFlags  flags;      ///< Attribute bitmask.
        u64          numa_node;  ///< NUMA node affinity (kNoNumaNode if N/A).
        u64          owner_id;   ///< Subsystem that owns this region (kNoOwner = unclaimed).

        // ----------------------------------------------------------------
        // Constructors
        // ----------------------------------------------------------------

        constexpr RegionDescriptor() noexcept
            : region(), type(RegionType::Generic), flags(RegionFlags::None),
              numa_node(kNoNumaNode), owner_id(kNoOwner)
        {}

        constexpr RegionDescriptor(
            void*       base,
            usize       size,
            RegionType  type_v    = RegionType::Generic,
            RegionFlags flags_v   = RegionFlags::None,
            u64         numa_v    = kNoNumaNode,
            u64         owner_v   = kNoOwner
        ) noexcept
            : region(base, size), type(type_v), flags(flags_v),
              numa_node(numa_v), owner_id(owner_v)
        {}

        /// @brief Construct from an existing MemoryRegion.
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

        // ----------------------------------------------------------------
        // Accessors (delegate to MemoryRegion)
        // ----------------------------------------------------------------

        [[nodiscard]] constexpr byte*  Base()                    const noexcept { return region.Base(); }
        [[nodiscard]] constexpr byte*  End()                     const noexcept { return region.End();  }
        [[nodiscard]] constexpr usize  Size()                    const noexcept { return region.Size(); }
        [[nodiscard]] constexpr bool   IsValid()                 const noexcept { return region.IsValid(); }
        [[nodiscard]] constexpr bool   Contains(const void* ptr) const noexcept { return region.Contains(ptr); }
        [[nodiscard]] constexpr bool   Overlaps(const RegionDescriptor& other) const noexcept {
            return region.Overlaps(other.region);
        }

        // ----------------------------------------------------------------
        // Attribute Queries
        // ----------------------------------------------------------------

        [[nodiscard]] constexpr bool IsReadable()   const noexcept { return HasRegionFlag(flags, RegionFlags::Readable); }
        [[nodiscard]] constexpr bool IsWritable()   const noexcept { return HasRegionFlag(flags, RegionFlags::Writable); }
        [[nodiscard]] constexpr bool IsExecutable() const noexcept { return HasRegionFlag(flags, RegionFlags::Executable); }
        [[nodiscard]] constexpr bool IsCacheable()  const noexcept { return HasRegionFlag(flags, RegionFlags::Cacheable); }
        [[nodiscard]] constexpr bool IsPinned()     const noexcept { return HasRegionFlag(flags, RegionFlags::Pinned); }
        [[nodiscard]] constexpr bool IsZeroed()     const noexcept { return HasRegionFlag(flags, RegionFlags::Zeroed); }
        [[nodiscard]] constexpr bool IsNumaAware()  const noexcept { return numa_node != kNoNumaNode; }
        [[nodiscard]] constexpr bool IsOwned()      const noexcept { return owner_id != kNoOwner; }

        [[nodiscard]] constexpr bool IsDmaCoherent() const noexcept {
            return type == RegionType::DmaCoherent;
        }

        [[nodiscard]] constexpr bool IsMmio() const noexcept {
            return type == RegionType::Mmio;
        }

        // ----------------------------------------------------------------
        // Sub-region Extraction
        // ----------------------------------------------------------------

        /// @brief Carve out a sub-descriptor at `offset` of `size` bytes.
        ///        Inherits all metadata from this descriptor.
        [[nodiscard]] constexpr RegionDescriptor SubDescriptor(usize offset, usize sub_size) const noexcept {
            return {region.SubRegion(offset, sub_size), type, flags, numa_node, owner_id};
        }

        // ----------------------------------------------------------------
        // Validation
        // ----------------------------------------------------------------

        void Verify() const noexcept {
            region.Verify();
            if (type == RegionType::Mmio) {
                FK_BUG_ON(HasRegionFlag(flags, RegionFlags::Cacheable),
                    "RegionDescriptor: MMIO region must not be cacheable");
            }
        }
    };

    // ============================================================================
    // RegionDescriptorPool<N> — Manage a table of region descriptors
    // ============================================================================

    /// @brief Static-capacity table of RegionDescriptors.
    /// @desc Useful for a kernel's physical memory map or NUMA node registry.
    template <usize N>
    class RegionDescriptorPool {
        static_assert(N >= 1, "RegionDescriptorPool: N must be >= 1");

    public:
        constexpr RegionDescriptorPool() noexcept = default;

        /// @brief Register a new descriptor. Returns the index assigned.
        [[nodiscard]] usize Register(const RegionDescriptor& desc) noexcept {
            FK_BUG_ON(m_count >= N,
                "RegionDescriptorPool: capacity ({}) exceeded", N);
            m_entries[m_count] = desc;
            return m_count++;
        }

        [[nodiscard]] RegionDescriptor& At(usize idx) noexcept {
            FK_BUG_ON(idx >= m_count, "RegionDescriptorPool::At: index out of bounds ({} >= {})", idx, m_count);
            return m_entries[idx];
        }

        [[nodiscard]] const RegionDescriptor& At(usize idx) const noexcept {
            FK_BUG_ON(idx >= m_count, "RegionDescriptorPool::At: index out of bounds ({} >= {})", idx, m_count);
            return m_entries[idx];
        }

        [[nodiscard]] usize Count()                     const noexcept { return m_count; }
        [[nodiscard]] static constexpr usize Capacity() noexcept { return N; }

        /// @brief Find the first descriptor containing `ptr`.
        /// @return Pointer to the descriptor, or nullptr if none found.
        [[nodiscard]] const RegionDescriptor* FindContaining(const void* ptr) const noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].Contains(ptr)) return &m_entries[i];
            }
            return nullptr;
        }

        /// @brief Find the first descriptor matching `type`.
        [[nodiscard]] const RegionDescriptor* FindByType(RegionType type) const noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].type == type) return &m_entries[i];
            }
            return nullptr;
        }

        /// @brief Find the first descriptor on `numa_node`.
        [[nodiscard]] const RegionDescriptor* FindByNuma(u64 numa_node) const noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].numa_node == numa_node) return &m_entries[i];
            }
            return nullptr;
        }

    private:
        RegionDescriptor m_entries[N] = {};
        usize            m_count      = 0;
    };

} // namespace FoundationKitMemory
