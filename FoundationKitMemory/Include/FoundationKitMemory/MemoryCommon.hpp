#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Result Types for Operations
    // ============================================================================

    /// @brief Allocation result with optional error information.
    using AllocationOption = Optional<void*>;

    /// @brief Allocation result with detailed error information.
    using AllocationExpected = Expected<void*, MemoryError>;

    // ============================================================================
    // Allocation Statistics
    // ============================================================================

    /// @brief Statistics tracked by stateful allocators.
    struct AllocationStats {
        usize bytes_allocated   = 0;   // Total bytes allocated
        usize bytes_deallocated = 0;   // Total bytes deallocated
        usize total_allocations = 0;   // Number of allocation calls
        usize total_deallocations = 0; // Number of deallocation calls
        usize peak_usage = 0;          // Peak bytes in use

        [[nodiscard]] constexpr usize CurrentUsage() const noexcept {
            return bytes_allocated - bytes_deallocated;
        }

        [[nodiscard]] constexpr usize UnreleasedCount() const noexcept {
            return total_allocations - total_deallocations;
        }

        void Reset() noexcept {
            bytes_allocated = 0;
            bytes_deallocated = 0;
            total_allocations = 0;
            total_deallocations = 0;
            peak_usage = 0;
        }
    };

    // ============================================================================
    // Utility: Align Pointer
    // ============================================================================

    /// @brief Align a pointer upward to the given alignment.
    [[nodiscard]] constexpr void* AlignPointer(void* ptr, usize align) noexcept {
        const auto aligned = Alignment(align).AlignUp(reinterpret_cast<uptr>(ptr));
        return reinterpret_cast<void*>(aligned);
    }

    /// @brief Calculate padding needed to align a pointer.
    [[nodiscard]] constexpr usize AlignmentPadding(uptr ptr, usize align) noexcept {
        return Alignment(align).AlignUp(ptr) - ptr;
    }

    /// @brief Check if pointer is aligned to the given alignment.
    [[nodiscard]] constexpr bool IsAligned(const void* ptr, usize align) noexcept {
        const auto u = reinterpret_cast<uptr>(ptr);
        return (u & (align - 1)) == 0;
    }

    // ============================================================================
    // Utility: Size Calculations
    // ============================================================================

    /// @brief Calculate total size needed for array with alignment padding.
    /// @param count Number of elements
    /// @param element_size Size of each element
    /// @param alignment Required alignment
    /// @return Total size including padding, or 0 if overflow detected
    [[nodiscard]] constexpr usize CalculateArraySize(usize count, usize element_size, usize alignment = 1) noexcept {
        constexpr usize USIZE_MAX_VAL = static_cast<usize>(-1);

        FK_BUG_ON(element_size == 0 && count > 0,
            "CalculateArraySize: element_size is zero with non-zero count ({})", count);
        FK_BUG_ON(alignment == 0,
            "CalculateArraySize: alignment must be >= 1");
        FK_BUG_ON(alignment > 1 && (alignment & (alignment - 1)) != 0,
            "CalculateArraySize: alignment ({}) must be a power of two", alignment);

        if (count == 0 || element_size == 0) return 0;

        if (element_size > USIZE_MAX_VAL / count) {
            return 0; // Overflow detected
        }

        usize base_size = count * element_size;

        if (alignment > 1 && base_size > 0) {
            usize remainder = base_size % alignment;
            if (remainder != 0) {
                usize padding = alignment - remainder;
                if (base_size > USIZE_MAX_VAL - padding) {
                    return 0; // Overflow when adding padding
                }
                base_size += padding;
            }
        }

        return base_size;
    }

    /// @brief Calculate total allocation size for T array.
    template <typename T>
    [[nodiscard]] constexpr usize CalculateArrayAllocationSize(usize count) noexcept {
        return CalculateArraySize(count, sizeof(T), alignof(T));
    }

} // namespace FoundationKitMemory
