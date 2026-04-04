#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Bit.hpp>
#include <FoundationKitCxxStl/Structure/BitSet.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    /// @brief A high-performance Buddy Allocator for power-of-two blocks.
    /// @desc Optimized for kernel page allocation. Uses two bitsets to track
    ///       allocated and split states.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<BuddyAllocator<MaxOrder, MinBlockSize>, SpinLock>
    template <usize MaxOrder = 10, usize MinBlockSize = 4096>
    class BuddyAllocator {
    public:
        static constexpr usize TotalNodes = (1ULL << (MaxOrder + 1)) - 1;
        static constexpr usize MaxBlockSize = MinBlockSize << MaxOrder;

        constexpr BuddyAllocator() noexcept = default;

        /// @brief Initialize with a memory region.
        void Initialize(void* start, usize size) noexcept {
            if (size < MaxBlockSize) return;
            m_start = static_cast<byte*>(start);
            m_allocated.Reset();
            m_split.Reset();
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size == 0 || size > MaxBlockSize || !m_start) 
                return AllocationResult::Failure(MemoryError::InvalidSize);

            // Determine required order
            usize order = 0;
            usize current_size = MinBlockSize;
            while (current_size < size || current_size < align) {
                current_size <<= 1;
                order++;
            }

            if (order > MaxOrder) return AllocationResult::Failure(MemoryError::AllocationTooLarge);

            const usize level = MaxOrder - order;
            
            i32 node_index = FindFreeNode(0, 0, level);
            if (node_index < 0) return AllocationResult::Failure(MemoryError::OutOfMemory);

            m_allocated.Set(static_cast<usize>(node_index));
            MarkAncestorsSplit(static_cast<usize>(node_index));

            const usize offset = GetNodeOffset(static_cast<usize>(node_index), level);
            return AllocationResult::Success(m_start + offset, current_size);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr || !m_start) return;

            const auto offset = static_cast<usize>(static_cast<byte*>(ptr) - m_start);
            FK_BUG_ON(offset >= MaxBlockSize, "BuddyAllocator: Pointer out of range ({}) >= ({})", offset, MaxBlockSize);

            usize order = 0;
            usize current_size = MinBlockSize;
            while (current_size < size) {
                current_size <<= 1;
                order++;
            }

            const usize level = MaxOrder - order;
            const usize node_index = (offset / (MaxBlockSize >> level)) + ((1ULL << level) - 1);

            FK_BUG_ON(!m_allocated.Test(node_index), "BuddyAllocator: Double free or invalid deallocation");

            m_allocated.Reset(node_index);
            UpdateAncestorsAfterFree(node_index);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return ptr >= m_start && ptr < m_start + MaxBlockSize;
        }

    private:
        [[nodiscard]] i32 FindFreeNode(usize index, usize level, usize target_level) const noexcept {
            // If this node is fully allocated, we can't use it or its children
            if (m_allocated.Test(index)) return -1;

            if (level == target_level) {
                // If it's already split, we can't allocate it as a whole block at this level
                if (m_split.Test(index)) return -1;
                return static_cast<i32>(index);
            }

            // Try to find in children
            i32 found = FindFreeNode(2 * index + 1, level + 1, target_level);
            if (found < 0) {
                found = FindFreeNode(2 * index + 2, level + 1, target_level);
            }
            return found;
        }

        void MarkAncestorsSplit(usize index) noexcept {
            while (index > 0) {
                index = (index - 1) / 2;
                m_split.Set(index);
            }
        }

        void UpdateAncestorsAfterFree(usize index) noexcept {
            while (index > 0) {
                const usize parent = (index - 1) / 2;
                const usize left = 2 * parent + 1;
                const usize right = 2 * parent + 2;

                // Parent is split if either child is allocated OR either child is split
                if (!m_allocated.Test(left) && !m_allocated.Test(right) &&
                    !m_split.Test(left) && !m_split.Test(right)) {
                    m_split.Reset(parent);
                    index = parent;
                } else {
                    break;
                }
            }
        }

        [[nodiscard]] constexpr usize GetNodeOffset(usize index, usize level) const noexcept {
            return (index - ((1ULL << level) - 1)) * (MaxBlockSize >> level);
        }

        byte* m_start = nullptr;
        Structure::BitSet<TotalNodes> m_allocated; // Head of an allocation
        Structure::BitSet<TotalNodes> m_split;     // Partially used by children
    };

} // namespace FoundationKitMemory
