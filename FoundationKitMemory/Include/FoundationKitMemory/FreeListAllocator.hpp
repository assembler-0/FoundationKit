#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryCommon.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // FreeList Node and Header
    // ============================================================================

    /// @brief Intrusive free-list node embedded in each free block.
    struct FreeListNode {
        usize       size;  ///< Total size of this free block (including this header).
        FreeListNode* next; ///< Next free block in address order.
    };

    /// @brief Allocation header stored immediately before each live payload.
    struct FreeListHeader {
        u32   magic;   ///< 0x46524545 ('FREE') — corruption sentinel.
        u32   padding; ///< Bytes from block start to this header.
        usize size;    ///< Total block size (header + padding + payload + split residual).
    };

    // ============================================================================
    // Allocation Policy Concepts
    // ============================================================================

    /// @brief Concept for a stateless allocation policy.
    /// @desc A stateless policy provides `Select` as a static member.
    template <typename P>
    concept StatelessFitPolicy = requires(FreeListNode*& head, FreeListNode*& prev_out, usize needed) {
        { P::Select(head, prev_out, needed) } -> SameAs<FreeListNode*>;
    };

    /// @brief Concept for a stateful allocation policy (e.g., NextFit cursor).
    template <typename P>
    concept StatefulFitPolicy = requires(P& p, FreeListNode*& head, FreeListNode*& prev_out, usize needed) {
        { p.Select(head, prev_out, needed) } -> SameAs<FreeListNode*>;
    };

    // ============================================================================
    // AllocationPolicy Implementations
    // ============================================================================

    /// @brief First-Fit: return the first free block that fits.
    /// @desc O(n) worst case, O(1) best case. Left-biases the free list.
    ///       Fast but degrades poorly under long-running mixed workloads.
    struct FirstFitPolicy {
        /// @param head      Head of the free list (unmodified).
        /// @param prev_out  Set to the node preceding the returned node (or nullptr if head).
        /// @param needed    Minimum block size required.
        /// @return Pointer to the chosen node, or nullptr if none found.
        static FreeListNode* Select(
            FreeListNode*& head,
            FreeListNode*& prev_out,
            usize          needed
        ) noexcept {
            FreeListNode* prev    = nullptr;
            FreeListNode* current = head;
            while (current) {
                if (current->size >= needed) {
                    prev_out = prev;
                    return current;
                }
                prev    = current;
                current = current->next;
            }
            prev_out = nullptr;
            return nullptr;
        }
    };

    /// @brief Best-Fit: return the smallest free block that fits.
    /// @desc O(n) always (must scan the full list). Minimises external fragmentation
    ///       by ~30–40% compared to first-fit in mixed-size workloads.
    struct BestFitPolicy {
        static FreeListNode* Select(
            FreeListNode*& head,
            FreeListNode*& prev_out,
            usize          needed
        ) noexcept {
            FreeListNode* best      = nullptr;
            FreeListNode* best_prev = nullptr;
            FreeListNode* prev      = nullptr;
            FreeListNode* current   = head;

            while (current) {
                if (current->size >= needed) {
                    // Prefer the tightest fit.
                    if (!best || current->size < best->size) {
                        best      = current;
                        best_prev = prev;
                    }
                }
                prev    = current;
                current = current->next;
            }

            prev_out = best_prev;
            return best;
        }
    };

    /// @brief Worst-Fit: return the largest free block.
    /// @desc Maximises the residual after allocation; useful for debugging
    ///       fragmentation patterns and as a stress test for allocator correctness.
    struct WorstFitPolicy {
        static FreeListNode* Select(
            FreeListNode*& head,
            FreeListNode*& prev_out,
            usize          needed
        ) noexcept {
            FreeListNode* worst      = nullptr;
            FreeListNode* worst_prev = nullptr;
            FreeListNode* prev       = nullptr;
            FreeListNode* current    = head;

            while (current) {
                if (current->size >= needed) {
                    if (!worst || current->size > worst->size) {
                        worst      = current;
                        worst_prev = prev;
                    }
                }
                prev    = current;
                current = current->next;
            }

            prev_out = worst_prev;
            return worst;
        }
    };

    /// @brief Next-Fit: stateful — resume searching from where the last allocation left off.
    /// @desc Distributes allocations more evenly across the free list than first-fit,
    ///       avoiding left-bias. Requires one extra pointer of state.
    struct NextFitPolicy {
        FreeListNode* m_last_pos = nullptr; ///< Cursor: start of next search.

        FreeListNode* Select(
            FreeListNode*& head,
            FreeListNode*& prev_out,
            usize          needed
        ) noexcept {
            if (!head) { prev_out = nullptr; return nullptr; }

            // Validate m_last_pos is still reachable; if the list changed, fall back to head.
            bool last_valid = false;
            {
                FreeListNode* scan = head;
                while (scan) {
                    if (scan == m_last_pos) { last_valid = true; break; }
                    scan = scan->next;
                }
            }
            if (!last_valid) m_last_pos = head;

            // Two-pass: start from m_last_pos, wrap around once.
            for (usize pass = 0; pass < 2; ++pass) {
                FreeListNode* prev    = nullptr;
                FreeListNode* current = (pass == 0) ? m_last_pos : head;

                while (current) {
                    if (current->size >= needed) {
                        m_last_pos = current->next ? current->next : head;
                        prev_out   = prev;
                        return current;
                    }
                    prev    = current;
                    current = current->next;
                }
            }

            prev_out = nullptr;
            return nullptr;
        }
    };

    // ============================================================================
    // PolicyFreeListAllocator<Policy>
    // ============================================================================

    /// @brief General-purpose allocator with pluggable allocation policy.
    /// @desc Manages a contiguous raw buffer through a sorted free list with
    ///       coalescing on deallocation. The search strategy is fully governed
    ///       by the `Policy` parameter — swap without changing any call-site code.
    ///
    /// @tparam Policy  One of: FirstFitPolicy, BestFitPolicy, WorstFitPolicy,
    ///                 NextFitPolicy, or any custom type satisfying StatefulFitPolicy.
    ///
    /// @warning NOT thread-safe. Wrap with:
    ///          SynchronizedAllocator<PolicyFreeListAllocator<P>, Mutex>
    template <typename Policy = FirstFitPolicy>
    class PolicyFreeListAllocator {
        static_assert(
            StatefulFitPolicy<Policy> || StatelessFitPolicy<Policy>,
            "PolicyFreeListAllocator: Policy must satisfy StatefulFitPolicy or StatelessFitPolicy"
        );

    public:
        static constexpr u32 HeaderMagic = 0x46524545; ///< 'FREE'

        constexpr PolicyFreeListAllocator() noexcept = default;

        constexpr PolicyFreeListAllocator(void* start, usize size) noexcept {
            Initialize(start, size);
        }

        /// @brief Initialize the allocator with a raw memory region.
        void Initialize(void* start, usize size) noexcept {
            if (!start || size < sizeof(FreeListNode)) {
                m_start     = nullptr;
                m_size      = 0;
                m_free_list = nullptr;
                return;
            }
            m_start           = static_cast<byte*>(start);
            m_size            = size;
            m_free_list       = reinterpret_cast<FreeListNode*>(m_start);
            m_free_list->size = size;
            m_free_list->next = nullptr;
        }

        // ----------------------------------------------------------------
        // IAllocator Interface
        // ----------------------------------------------------------------

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size == 0 || !m_free_list)
                return AllocationResult::Failure();

            FreeListNode* prev    = nullptr;
            FreeListNode* chosen  = nullptr;

            // For each candidate, compute total_needed = padding + size.
            // We pre-scan to find the best candidate per policy.
            // Policy::Select receives a mutated view — we must actually run the
            // geometry check inside the loop here rather than delegating the full
            // check to the policy (policy operates on raw node sizes).
            //
            // Strategy: call policy.Select iteratively, skipping nodes that fail
            // the geometry check, until a valid one is found or the list is exhausted.
            FreeListNode* scan_head = m_free_list;

            while (true) {
                FreeListNode* prev_cand = nullptr;
                FreeListNode* candidate = CallPolicy(scan_head, prev_cand, size + align + sizeof(FreeListHeader));
                if (!candidate) break;

                const uptr   cur_ptr     = reinterpret_cast<uptr>(candidate);
                const uptr   payload_ptr = Alignment(align).AlignUp(cur_ptr + sizeof(FreeListHeader));
                const auto   padding     = static_cast<u32>(payload_ptr - cur_ptr);
                const usize  total       = static_cast<usize>(padding) + size;

                if (candidate->size >= total) {
                    prev   = prev_cand;
                    chosen = candidate;
                    break;
                }

                // This candidate failed geometry — advance scan past it.
                // For best/worst-fit policies this shouldn't happen unless the policy
                // returned an undersized block (shouldn't, but guard anyway).
                if (candidate->next) {
                    scan_head = candidate->next;
                } else {
                    break;
                }
            }

            if (!chosen) return AllocationResult::Failure();

            const uptr   chosen_ptr  = reinterpret_cast<uptr>(chosen);
            const uptr   payload_ptr = Alignment(align).AlignUp(chosen_ptr + sizeof(FreeListHeader));
            const auto   padding     = static_cast<u32>(payload_ptr - chosen_ptr);
            const usize  total       = static_cast<usize>(padding) + size;

            // Split the block if the residual can hold another FreeListNode.
            if (chosen->size >= total + sizeof(FreeListNode)) {
                FreeListNode* next_node = reinterpret_cast<FreeListNode*>(chosen_ptr + total);
                next_node->size = chosen->size - total;
                next_node->next = chosen->next;

                if (prev) prev->next   = next_node;
                else      m_free_list  = next_node;
            } else {
                // Consume the whole block.
                if (prev) prev->next   = chosen->next;
                else      m_free_list  = chosen->next;
            }

            // Write the allocation header immediately before the payload.
            auto* header    = reinterpret_cast<FreeListHeader*>(payload_ptr - sizeof(FreeListHeader));
            header->magic   = HeaderMagic;
            header->padding = padding;
            header->size    = total;

            return AllocationResult::Success(reinterpret_cast<void*>(payload_ptr), size);
        }

        void Deallocate(void* ptr, usize /*size*/) noexcept {
            Deallocate(ptr);
        }

        /// @brief Size-less deallocation (uses inline header).
        void Deallocate(void* ptr) noexcept {
            if (!ptr) return;

            auto* header = reinterpret_cast<FreeListHeader*>(
                static_cast<byte*>(ptr) - sizeof(FreeListHeader));
            FK_BUG_ON(header->magic != HeaderMagic,
                "PolicyFreeListAllocator: header magic mismatch "
                "(expected: {:x} got: {:x})", HeaderMagic, header->magic);

            const uptr  block_start = reinterpret_cast<uptr>(ptr) - header->padding;
            const usize block_size  = header->size;

            FreeListNode* new_node = reinterpret_cast<FreeListNode*>(block_start);
            new_node->size = block_size;
            new_node->next = nullptr;

            // Insert in address order to facilitate coalescing.
            if (!m_free_list || block_start < reinterpret_cast<uptr>(m_free_list)) {
                new_node->next = m_free_list;
                m_free_list    = new_node;
            } else {
                FreeListNode* current = m_free_list;
                while (current->next &&
                       reinterpret_cast<uptr>(current->next) < block_start)
                {
                    current = current->next;
                }
                new_node->next = current->next;
                current->next  = new_node;
            }

            Coalesce();
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return ptr >= m_start && ptr < m_start + m_size;
        }

        /// @brief Merge adjacent free blocks; called automatically on Deallocate.
        void Coalesce() noexcept {
            FreeListNode* current = m_free_list;
            while (current && current->next) {
                const uptr current_end =
                    reinterpret_cast<uptr>(current) + current->size;
                if (current_end == reinterpret_cast<uptr>(current->next)) {
                    current->size += current->next->size;
                    current->next  = current->next->next;
                } else {
                    current = current->next;
                }
            }
        }

        [[nodiscard]] usize UsedMemory() const noexcept {
            usize free_bytes = 0;
            const FreeListNode* current = m_free_list;
            while (current) {
                free_bytes += current->size;
                current     = current->next;
            }
            return m_size - free_bytes;
        }

        [[nodiscard]] usize TotalSize()    const noexcept { return m_size; }
        [[nodiscard]] const FreeListNode* FreeListHead() const noexcept { return m_free_list; }

    private:
        // Dispatch: stateful vs. stateless policies.
        FreeListNode* CallPolicy(FreeListNode*& head, FreeListNode*& prev_out, usize needed) noexcept {
            if constexpr (StatefulFitPolicy<Policy>) {
                return m_policy.Select(head, prev_out, needed);
            } else {
                return Policy::Select(head, prev_out, needed);
            }
        }

        byte*         m_start     = nullptr;
        usize         m_size      = 0;
        FreeListNode* m_free_list = nullptr;
        [[no_unique_address]] Policy m_policy{};
    };

    // ============================================================================
    // Backwards-Compatible Alias
    // ============================================================================

    /// @brief Default alias: preserves the existing `FreeListAllocator` name.
    /// @note  Use `PolicyFreeListAllocator<BestFitPolicy>` explicitly for better behaviour.
    using FreeListAllocator = PolicyFreeListAllocator<FirstFitPolicy>;

    // ============================================================================
    // Static Assertions
    // ============================================================================

    static_assert(IAllocator<PolicyFreeListAllocator<FirstFitPolicy>>);
    static_assert(IAllocator<PolicyFreeListAllocator<BestFitPolicy>>);
    static_assert(IAllocator<PolicyFreeListAllocator<WorstFitPolicy>>);
    static_assert(IAllocator<PolicyFreeListAllocator<NextFitPolicy>>);
    static_assert(IAllocator<FreeListAllocator>);

} // namespace FoundationKitMemory
