#pragma once

#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitMemory/Allocators/FreeListAllocator.hpp>
#include <FoundationKitMemory/Allocators/BuddyAllocator.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // FragmentationReport
    // ============================================================================

    /// @brief Snapshot of an allocator's fragmentation state.
    /// @desc Zero overhead when not queried — no instrumentation in the hot path.
    struct FragmentationReport {
        usize total_bytes          = 0; ///< Total bytes in the managed region.
        usize used_bytes           = 0; ///< Bytes currently allocated to users.
        usize free_bytes           = 0; ///< Total free bytes (may be split into many blocks).
        usize largest_free_block   = 0; ///< Size of the contiguous block that satisfies the largest request.
        usize free_block_count     = 0; ///< Number of disjoint free blocks.
        usize internal_waste       = 0; ///< Bytes used for headers/alignment padding (reported by allocators that track this).

        /// @brief Fragmentation index in [0.0, 1.0].
        /// @desc 0.0 = perfectly defragmented (only one free block, or fully used).
        ///       1.0 = maximally fragmented (many tiny free blocks, none usable).
        ///       Formula: 1 - (largest_free_block / free_bytes), clamped to [0,1].
        [[nodiscard]] float FragmentationIndex() const noexcept {
            if (free_bytes == 0) return 0.0f;
            const float ratio = static_cast<float>(largest_free_block) /
                                static_cast<float>(free_bytes);
            return 1.0f - (ratio < 1.0f ? ratio : 1.0f);
        }

        /// @brief True if the heap can satisfy an allocation of `size` bytes.
        [[nodiscard]] bool CanSatisfy(usize size) const noexcept {
            return largest_free_block >= size;
        }

        /// @brief Percentage of the heap that is in use (0–100).
        [[nodiscard]] usize UsagePercent() const noexcept {
            if (total_bytes == 0) return 0;
            return (used_bytes * 100) / total_bytes;
        }
    };

    // ============================================================================
    // AnalyzeFragmentation — FreeList family
    // ============================================================================

    /// @brief Analyse fragmentation of any PolicyFreeListAllocator.
    /// @desc Walks the free list in O(n). Call only for diagnostics, not on the hot path.
    template <typename Policy>
    [[nodiscard]] FragmentationReport
    AnalyzeFragmentation(const PolicyFreeListAllocator<Policy>& alloc) noexcept {
        FragmentationReport report;
        report.total_bytes = alloc.TotalSize();

        const FreeListNode* node = alloc.FreeListHead();
        while (node) {
            report.free_bytes += node->size;
            ++report.free_block_count;
            if (node->size > report.largest_free_block)
                report.largest_free_block = node->size;
            node = node->next;
        }

        report.used_bytes = report.total_bytes - report.free_bytes;
        return report;
    }

    // ============================================================================
    // AnalyzeFragmentation — BuddyAllocator family
    // ============================================================================

    /// @brief Analyse fragmentation of a BuddyAllocator.
    /// @desc Walks all per-order free lists in O(MaxOrder * n_free).
    template <usize MaxOrder, usize MinBlockSize>
    [[nodiscard]] FragmentationReport
    AnalyzeFragmentation(const BuddyAllocator<MaxOrder, MinBlockSize>& alloc) noexcept {
        FragmentationReport report;
        report.total_bytes = BuddyAllocator<MaxOrder, MinBlockSize>::MaxBlockSize;

        const auto stats       = alloc.GetFreeStats();
        report.free_bytes      = stats.free_bytes;
        report.free_block_count = stats.free_block_count;
        report.largest_free_block = stats.largest_free;
        report.used_bytes      = report.total_bytes - report.free_bytes;
        return report;
    }

} // namespace FoundationKitMemory
