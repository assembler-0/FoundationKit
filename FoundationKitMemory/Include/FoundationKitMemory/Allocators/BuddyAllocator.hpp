#pragma once

#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Bit.hpp>
#include <FoundationKitCxxStl/Structure/BitSet.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // BuddyAllocator<MaxOrder, MinBlockSize>
    // ============================================================================

    /// @brief A high-performance Buddy Allocator for power-of-two blocks.
    /// @desc The classic kernel general-purpose heap algorithm used by Linux kmalloc tiers.
    ///       Allocates in powers-of-two; splits and merges blocks in O(log n).
    ///
    ///       Implementation notes:
    ///       - Uses per-order free lists stored as an intrusive singly-linked list
    ///         overlaid on the free block memory itself (no external metadata needed).
    ///       - Split: pop from free_list[order], push both halves into free_list[order-1].
    ///       - Merge: after free, XOR-compute buddy address; if buddy is also free
    ///                (checked via m_free_bitmap), merge and ascend.
    ///       - FULLY ITERATIVE — zero recursion. Critical for kernel stack safety.
    ///
    /// @tparam MaxOrder      Maximum order exponent; block size = MinBlockSize << MaxOrder.
    ///                       Defaults to 10 (1024 pages × 4KB = 4MB).
    /// @tparam MinBlockSize  Smallest allocatable block in bytes (must be power-of-two
    ///                       and >= sizeof(void*)). Defaults to 4096 (4KB page).
    ///
    /// @warning NOT thread-safe. Wrap with:
    ///          SynchronizedAllocator<BuddyAllocator<>, SpinLock>
    template <usize MaxOrder = 10, usize MinBlockSize = 4096>
    class BuddyAllocator {
        // AssertPowerOfTwo fires at instantiation for bad MinBlockSize values.
        static_assert((MinBlockSize & (MinBlockSize - 1)) == 0,
            "BuddyAllocator: MinBlockSize must be a power of two");
        static_assert(MinBlockSize >= sizeof(void*),
            "BuddyAllocator: MinBlockSize must be large enough to hold a free-list pointer");
        static_assert(MaxOrder >= 1,
            "BuddyAllocator: MaxOrder must be at least 1");
        static_assert(MaxOrder <= 20,
            "BuddyAllocator: MaxOrder > 20 is unreasonably large");

    public:
        /// @brief Total managed bytes (order MaxOrder block).
        static constexpr usize MaxBlockSize = MinBlockSize << MaxOrder;

        /// @brief Total node count in the binary-tree bitmap.
        ///        Order k has (1 << k) nodes; sum over 0..MaxOrder = 2^(MaxOrder+1) - 1.
        static constexpr usize TotalBitmapNodes = (1ULL << (MaxOrder + 1)) - 1;

        constexpr BuddyAllocator() noexcept = default;

        /// @brief Initialize with a memory region.
        /// @param start Beginning of the memory pool (must be aligned to MaxBlockSize).
        /// @param size  Must be >= MaxBlockSize.
        void Initialize(void* start, usize size) noexcept {
            FK_BUG_ON(!start && size > 0, "BuddyAllocator::Initialize: null buffer provided with size {}", size);
            if (!start || size < MinBlockSize) {
                if (size > 0 && size < MinBlockSize) {
                    FK_BUG("BuddyAllocator::Initialize: buffer ({}) < MinBlockSize ({})", size, MinBlockSize);
                }
                return;
            }
            m_start = static_cast<byte*>(start);
            m_size  = size;
            FK_BUG_ON(size > MaxBlockSize,
                "BuddyAllocator::Initialize: buffer size ({}) exceeds MaxBlockSize ({}) for order {}. "
                "Increase MaxOrder to support larger zones.",
                size, MaxBlockSize, MaxOrder);

            // We start with a clean bitmap (all 0 = all free).
            // This is critical because the buddy algorithm logic expects sub-blocks of
            // free blocks to be marked as free.
            m_free_bitmap.Reset();
            m_free_bytes  = 0;
            m_free_blocks = 0;

            for (usize i = 0; i <= MaxOrder; ++i) m_free_lists[i] = nullptr;

            // 1. Fill free lists with usable memory chunks.
            usize remaining = m_size;
            byte* current   = m_start;

            while (remaining >= MinBlockSize) {
                usize order = 0;
                while (order < MaxOrder) {
                    const usize next_size = MinBlockSize << (order + 1);

                    if (const auto offset = static_cast<usize>(current - m_start);
                        next_size > remaining || (offset % next_size) != 0) {
                        break;
                    }
                    ++order;
                }

                const usize block_size = MinBlockSize << order;
                PushFree(order, current);
                m_free_bytes  += block_size;
                m_free_blocks += 1;
                current   += block_size;
                remaining -= block_size;
            }

            // 2. Mark the "phantom" range (beyond m_size up to MaxBlockSize) as allocated.
            // This prevents merging into unavailable physical memory.
            if (m_size < MaxBlockSize) {
                usize phantom_remaining = MaxBlockSize - m_size;
                byte* phantom_current   = m_start + m_size;

                while (phantom_remaining >= MinBlockSize) {
                    usize order = 0;
                    while (order < MaxOrder) {
                        const usize next_size = MinBlockSize << (order + 1);

                        if (const auto offset = static_cast<usize>(phantom_current - m_start);
                            next_size > phantom_remaining || (offset % next_size) != 0) {
                            break;
                        }
                        ++order;
                    }

                    // Mark this block as allocated in the bitmap.
                    m_free_bitmap.Set(BlockToNode(phantom_current, order));

                    const usize block_size = MinBlockSize << order;
                    phantom_current   += block_size;
                    phantom_remaining -= block_size;
                }
            }
        }

        /// @brief Allocate a block of at least `size` bytes with `align` alignment.
        /// @desc Alignment must be <= (MinBlockSize << required_order). Because buddy
        ///       blocks are naturally aligned to their own size, any alignment requirement
        ///       is satisfied by rounding up to the next order if needed.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size == 0 || !m_start)
                return AllocationResult::Failure(MemoryError::InvalidSize);

            // Find the minimum order whose block size satisfies both size and alignment.
            usize order        = 0;
            usize block_size   = MinBlockSize;
            while (block_size < size || block_size < align) {
                block_size <<= 1;
                ++order;
            }

            if (order > MaxOrder)
                return AllocationResult::Failure(MemoryError::AllocationTooLarge);

            // Walk up from `order` to MaxOrder looking for a free block.
            usize found_order = order;
            while (found_order <= MaxOrder && m_free_lists[found_order] == nullptr)
                ++found_order;

            if (found_order > MaxOrder)
                return AllocationResult::Failure(MemoryError::OutOfMemory);

            // Pop the free block from found_order.
            byte* block = PopFree(found_order);
            m_free_bytes  -= (MinBlockSize << found_order);
            m_free_blocks -= 1;

            // Split down from found_order to the requested order.
            while (found_order > order) {
                --found_order;
                const usize size = MinBlockSize << found_order;
                byte* buddy = block + size;
                PushFree(found_order, buddy);
                m_free_bytes  += size;
                m_free_blocks += 1;
            }

            // Mark allocated in bitmap.
            const usize node = BlockToNode(block, order);
            FK_BUG_ON(m_free_bitmap.Test(node),
                "BuddyAllocator: Allocate found double-allocated node at order {}", order);
            m_free_bitmap.Set(node);

            return AllocationResult::Success(block, block_size);
        }

        /// @brief Deallocate a previously allocated block.
        /// @param ptr  Pointer returned by Allocate().
        /// @param size Must be the exact size that was returned by Allocate (or the
        ///             user-requested size — we round up to the same order).
        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr || !m_start) return;

            const auto offset = static_cast<usize>(static_cast<byte*>(ptr) - m_start);
            FK_BUG_ON(offset >= m_size,
                "BuddyAllocator: Deallocate pointer out of range (offset: {} >= {})",
                offset, m_size);

            // Reconstruct the order (same rounding as Allocate).
            usize order      = 0;
            usize block_size = MinBlockSize;
            while (block_size < size) {
                block_size <<= 1;
                ++order;
            }

            FK_BUG_ON(order > MaxOrder,
                "BuddyAllocator: Deallocate order ({}) > MaxOrder ({})", order, MaxOrder);

            const usize node = BlockToNode(static_cast<byte*>(ptr), order);
            FK_BUG_ON(!m_free_bitmap.Test(node),
                "BuddyAllocator: Double-free or invalid deallocation at order {}", order);
            m_free_bitmap.Reset(node);

            auto* block = static_cast<byte*>(ptr);

            // Iteratively merge with buddy if buddy is free.
            while (order < MaxOrder) {
                const usize block_bytes = MinBlockSize << order;

                // Buddy address: XOR the block address with its own size.
                // This works because blocks at order `k` are aligned to `block_bytes`,
                // so flipping the bit at position log2(block_bytes) gives the buddy.
                const auto block_offset = static_cast<usize>(block - m_start);
                const usize buddy_offset = block_offset ^ block_bytes;
                byte* buddy = m_start + buddy_offset;

                // Check if the buddy is free (not allocated).
                const usize buddy_node = BlockToNode(buddy, order);
                if (m_free_bitmap.Test(buddy_node)) {
                    // Buddy is allocated — cannot merge.
                    break;
                }

                // Check that the buddy is actually in our free list at this order
                // (it might be split, in which case it wouldn't be a free full block).
                if (!IsBuddyFreeAtOrder(buddy, order)) {
                    break;
                }

                // Remove buddy from the free list at this order.
                RemoveFree(order, buddy);
                m_free_bytes  -= block_bytes;
                m_free_blocks -= 1;

                // Merge: the canonical block is always at the lower address.
                if (buddy < block) block = buddy;

                ++order;
            }

            PushFree(order, block);
            m_free_bytes  += (MinBlockSize << order);
            m_free_blocks += 1;
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_start && ptr >= m_start && ptr < m_start + m_size;
        }

        /// @brief Report the smallest free block size (for telemetry).
        [[nodiscard]] usize SmallestFreeBlockSize() const noexcept {
            for (usize i = 0; i <= MaxOrder; ++i) {
                if (m_free_lists[i]) return MinBlockSize << i;
            }
            return 0;
        }

        /// @brief Report the largest free block size (for telemetry).
        [[nodiscard]] usize LargestFreeBlockSize() const noexcept {
            for (usize i = MaxOrder + 1; i-- > 0;) {
                if (m_free_lists[i]) return MinBlockSize << i;
            }
            return 0;
        }

        /// @brief Aggregate free-list statistics for fragmentation analysis.
        struct FreeStats {
            usize free_bytes       = 0;
            usize free_block_count = 0;
            usize largest_free     = 0;
        };

        /// @brief Walk all per-order free lists and accumulate byte totals.
        /// @desc O(MaxOrder * n_free). Call only from diagnostic paths.
        [[nodiscard]] FreeStats GetFreeStats() const noexcept {
            FreeStats s;
            s.free_bytes       = m_free_bytes;
            s.free_block_count = m_free_blocks;
            s.largest_free     = LargestFreeBlockSize();
            return s;
        }

    private:
        // ----------------------------------------------------------------
        // Free List Intrusive Node
        // ----------------------------------------------------------------
        // Free blocks store a single `void*` pointer at their start,
        // forming a singly-linked list. This costs zero external memory.
        struct FreeNode { FreeNode* next; };

        // ----------------------------------------------------------------
        // Bitmap Node Index Computation
        // ----------------------------------------------------------------
        // The binary buddy tree has 2^(MaxOrder+1) - 1 nodes.
        // Order `k` occupies level (MaxOrder - k) in the tree.
        // The first node at level `lv` is (1 << lv) - 1.
        // The in-level index of a block is offset / (MaxBlockSize >> lv).
        [[nodiscard]] constexpr usize BlockToNode(const byte* block, usize order) const noexcept {
            const usize level     = MaxOrder - order;   // depth in tree
            const usize lv_first  = (1ULL << level) - 1;
            const usize block_size = MaxBlockSize >> level;
            const usize in_level  = static_cast<usize>(block - m_start) / block_size;
            return lv_first + in_level;
        }

        // ----------------------------------------------------------------
        // Per-Order Free List Operations
        // ----------------------------------------------------------------
        void PushFree(usize order, byte* block) noexcept {
            auto* node      = reinterpret_cast<FreeNode*>(block);
            node->next      = m_free_lists[order];
            m_free_lists[order] = node;
        }

        [[nodiscard]] byte* PopFree(usize order) noexcept {
            FreeNode* node      = m_free_lists[order];
            FK_BUG_ON(!node, "BuddyAllocator: PopFree called on empty list at order {}", order);
            m_free_lists[order] = node->next;
            return reinterpret_cast<byte*>(node);
        }

        void RemoveFree(usize order, byte* target) noexcept {
            FreeNode** curr = &m_free_lists[order];
            while (*curr) {
                if (reinterpret_cast<byte*>(*curr) == target) {
                    *curr = (*curr)->next;
                    return;
                }
                curr = &(*curr)->next;
            }
            FK_BUG("BuddyAllocator: RemoveFree: block not found in free list at order {}", order);
        }

        /// @brief Check if a specific block address appears in the free list at `order`.
        [[nodiscard]] bool IsBuddyFreeAtOrder(byte* target, usize order) const noexcept {
            const FreeNode* curr = m_free_lists[order];
            while (curr) {
                if (reinterpret_cast<const byte*>(curr) == target) return true;
                curr = curr->next;
            }
            return false;
        }

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        byte*     m_start                 = nullptr;
        usize     m_size                  = 0;
        usize     m_free_bytes            = 0;
        usize     m_free_blocks           = 0;
        FreeNode* m_free_lists[MaxOrder + 1] = {};

        /// @brief Bitmap: bit set = block is fully allocated (leaf allocation or
        ///        internal node fully consumed). Used for double-free detection.
        Structure::BitSet<TotalBitmapNodes> m_free_bitmap;
    };

    static_assert(IAllocator<BuddyAllocator<>>);

} // namespace FoundationKitMemory
