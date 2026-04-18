#pragma once

#include <FoundationKitMemory/Core/MemoryCore.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Memory Region: Type-Safe Bounded Memory Block
    // ============================================================================

    /// @brief Type-safe memory region descriptor.
    /// @desc Represents a contiguous memory block with ownership tracking.
    ///       Prevents allocations outside the region and deallocations across regions.
    class MemoryRegion {
    public:
        /// @brief Magic number for corruption detection.
        static constexpr u32 kMagic = 0x5245474D; // 'REGM'

        /// @brief Create a memory region from raw pointer and size.
        /// @param base Starting address (must be non-null if size > 0)
        /// @param size Region size in bytes
        constexpr MemoryRegion(void* base, usize size) noexcept
            : m_base(static_cast<byte*>(base)), m_size(size), m_magic(kMagic) {
            FK_BUG_ON(base == nullptr && size > 0, 
                "MemoryRegion: null base with non-zero size ({})", size);
            
            if (base != nullptr) {
                FK_BUG_ON(reinterpret_cast<uptr>(base) + size < reinterpret_cast<uptr>(base),
                    "MemoryRegion: size ({}) causes address space wraparound from ({})", size, base);
            }
        }

        /// @brief Create a null (empty) region.
        constexpr MemoryRegion() noexcept : m_magic(kMagic) {}

        /// @brief Verify region integrity.
        void Verify() const noexcept {
            if (m_magic != kMagic) [[unlikely]] {
                FK_BUG("MemoryRegion: corruption detected - magic mismatch (found: {:#x}, expected: {:#x}) at region {}", 
                    m_magic, kMagic, *this);
            }
            if (m_base == nullptr && m_size > 0) [[unlikely]] {
                FK_BUG("MemoryRegion: inconsistent state - null base with non-zero size ({})", m_size);
            }
        }

        /// @brief Get the start address of this region.
        [[nodiscard]] constexpr byte* Base() const noexcept { 
            Verify();
            return m_base; 
        }

        /// @brief Get the end address (exclusive) of this region.
        [[nodiscard]] constexpr byte* End() const noexcept { 
            Verify();
            return m_base + m_size; 
        }

        /// @brief Get the total size of this region.
        [[nodiscard]] constexpr usize Size() const noexcept { 
            Verify();
            return m_size; 
        }

        /// @brief Check if a pointer belongs to this region.
        /// @param ptr The pointer to test (may be const)
        /// @return true if ptr is in [Base(), End())
        [[nodiscard]] constexpr bool Contains(const void* ptr) const noexcept {
            Verify();
            const auto* p = static_cast<const byte*>(ptr);
            return p >= m_base && p < m_base + m_size;
        }

        /// @brief Check if this region is valid (size > 0).
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            Verify();
            return m_base != nullptr && m_size > 0;
        }

        /// @brief Split this region into two parts at offset.
        /// @param offset The split point (must be <= Size())
        /// @return {left: [base, offset), right: [base+offset, end)}
        [[nodiscard]] constexpr MemoryRegion Split(usize offset) const noexcept {
            Verify();
            FK_BUG_ON(offset > m_size, "MemoryRegion::Split: offset ({}) exceeds size ({})", offset, m_size);
            return {m_base + offset, m_size - offset};
        }

        /// @brief Create a sub-region within this region.
        /// @param offset Starting offset within this region
        /// @param size Size of the sub-region
        /// @return New region [base+offset, base+offset+size)
        [[nodiscard]] constexpr MemoryRegion SubRegion(usize offset, usize size) const noexcept {
            Verify();
            FK_BUG_ON(offset > m_size, "MemoryRegion::SubRegion: offset ({}) exceeds region size ({})", offset, m_size);
            FK_BUG_ON(size > m_size - offset, 
                "MemoryRegion::SubRegion: sub-region size ({}) exceeds remaining space ({}) from offset ({})", 
                size, m_size - offset, offset);
            return {m_base + offset, size};
        }

        /// @brief Check if two regions overlap.
        /// @param other The region to test against
        /// @return true if regions have any byte in common
        [[nodiscard]] constexpr bool Overlaps(const MemoryRegion& other) const noexcept {
            Verify();
            other.Verify();
            return !(End() <= other.Base() || Base() >= other.End());
        }

    private:
        byte* m_base  = nullptr;
        usize m_size  = 0;
        u32   m_magic = 0;
    };

    // MemoryRegion contains: byte* (8B) + usize (8B) + u32 m_magic (4B) + 4B padding = 24B on LP64.
    // The static_assert enforces that no surprise fields have been added.
    static_assert(sizeof(MemoryRegion) == sizeof(byte*) + sizeof(usize) + sizeof(u32) + sizeof(u32),
        "MemoryRegion: layout changed unexpectedly (expected ptr + usize + u32 + u32 padding)");

    // ============================================================================
    // Concept: Region-Aware Allocator
    // ============================================================================

    /// @brief Extended allocator that tracks which region it owns.
    /// @desc Adds region metadata to standard IAllocator.
    template <typename A>
    concept IRegionAwareAllocator = IAllocator<A> && requires(const A& alloc) {
        /// @brief Get the memory region this allocator manages.
        { alloc.Region() } -> SameAs<MemoryRegion>;
    };

    // ============================================================================
    // Region-Aware Wrapper: Bind Allocator to Region
    // ============================================================================

    /// @brief Wraps any IAllocator to add region awareness.
    /// @tparam Alloc Must satisfy IAllocator<Alloc>
    template <IAllocator Alloc>
    class RegionAwareAllocator {
    public:
        /// @brief Create a region-aware wrapper.
        /// @param allocator Reference to the underlying allocator
        /// @param region The memory region this allocator should work within
        constexpr RegionAwareAllocator(Alloc& allocator, MemoryRegion region) noexcept
            : m_allocator(allocator), m_region(region) {
            FK_BUG_ON(!region.IsValid(), 
                "RegionAwareAllocator: region must be valid");
        }

        /// @brief Allocate from this region with bounds checking.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            AllocationResult result = m_allocator.Allocate(size, align);
            if (result.IsSuccess()) {
                FK_BUG_ON(!m_region.Contains(result.ptr), 
                    "RegionAwareAllocator: allocator returned pointer outside region");
            }
            return result;
        }

        /// @brief Deallocate, with ownership validation.
        void Deallocate(void* ptr, usize size) noexcept {
            FK_BUG_ON(ptr != nullptr && !m_region.Contains(ptr), 
                "RegionAwareAllocator: cannot deallocate pointer outside this region");
            m_allocator.Deallocate(ptr, size);
        }

        /// @brief Check ownership within this region.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_region.Contains(ptr) && m_allocator.Owns(ptr);
        }

        /// @brief Get the underlying region.
        [[nodiscard]] constexpr MemoryRegion Region() const noexcept {
            return m_region;
        }

        /// @brief Get reference to the underlying allocator.
        [[nodiscard]] constexpr Alloc& GetUnderlying() noexcept { return m_allocator; }
        [[nodiscard]] constexpr const Alloc& GetUnderlying() const noexcept { return m_allocator; }

    private:
        Alloc& m_allocator;
        MemoryRegion m_region;
    };

    /// @brief Bind any allocator to a region. Deduction helper.
    template <IAllocator Alloc>
    [[nodiscard]] constexpr RegionAwareAllocator<Alloc>
    MakeRegionAware(Alloc& alloc, MemoryRegion region) noexcept {
        return RegionAwareAllocator<Alloc>(alloc, region);
    }

    // ============================================================================
    // Region Pool: Manage Multiple Sub-Regions
    // ============================================================================

    /// @brief Divides a large region into fixed-size sub-regions.
    /// @desc Useful for carving out DMA zones, MMIO buffers, or per-CPU heaps.
    template <usize NumRegions>
    class RegionPool {
    public:
        /// @brief Partition a region into NumRegions equal parts.
        /// @param base Starting address
        /// @param total_size Total size to partition
        explicit constexpr RegionPool(void* base, usize total_size) noexcept
            : m_base_region(base, total_size) {
            FK_BUG_ON(total_size == 0 || NumRegions == 0, 
                "RegionPool: cannot partition empty region (size: {} regions: {})", total_size, NumRegions);

            const usize region_size = total_size / NumRegions;
            FK_BUG_ON(region_size == 0, 
                "RegionPool: region_size underflow (too many regions)");
            
            for (usize i = 0; i < NumRegions; ++i) {
                m_regions[i] = m_base_region.SubRegion(i * region_size, region_size);
            }
        }

        /// @brief Get a specific sub-region by index.
        /// @param idx Index (0 <= idx < NumRegions)
        [[nodiscard]] constexpr MemoryRegion At(usize idx) const noexcept {
            FK_BUG_ON(idx >= NumRegions, "RegionPool::At: index ({}) out of bounds ({})", idx, NumRegions);
            return m_regions[idx];
        }

        /// @brief Get the count of partitions.
        [[nodiscard]] static constexpr usize Count() noexcept { return NumRegions; }

        /// @brief Find which region contains a pointer.
        /// @return Region index if found, or NumRegions if not found
        [[nodiscard]] constexpr usize FindRegion(const void* ptr) const noexcept {
            for (usize i = 0; i < NumRegions; ++i) {
                if (m_regions[i].Contains(ptr)) {
                    return i;
                }
            }
            return NumRegions;  // Not found sentinel
        }

    private:
        MemoryRegion m_base_region;
        MemoryRegion m_regions[NumRegions];
    };

} // namespace FoundationKitMemory

// ============================================================================
// Formatter<MemoryRegion> — must live in the namespace where Formatter<T> is
//                           defined (FoundationKitCxxStl), not in FoundationKitMemory.
// ============================================================================

namespace FoundationKitCxxStl {
    /// @brief Formatter for FoundationKitMemory::MemoryRegion.
    template <>
    struct Formatter<FoundationKitMemory::MemoryRegion> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitMemory::MemoryRegion& value,
                    const FormatSpec& spec = {}) const noexcept {
            if (!value.IsValid()) {
                sb.Append("[Empty Region]", 14);
                return;
            }
            sb.Append('[');
            Formatter<byte*>().Format(sb, value.Base(), spec);
            sb.Append('-');
            Formatter<byte*>().Format(sb, value.End(), spec);
            sb.Append(" (", 2);
            Formatter<usize>().Format(sb, value.Size(), spec);
            sb.Append(")]", 2);
        }
    };
} // namespace FoundationKitCxxStl
