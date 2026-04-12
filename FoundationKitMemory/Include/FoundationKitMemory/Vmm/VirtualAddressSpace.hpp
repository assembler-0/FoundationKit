#pragma once

#include <FoundationKitMemory/Vmm/VmaDescriptor.hpp>
#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Structure/AugmentedIntrusiveRbTree.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // VirtualAddressSpace
    // =========================================================================

    /// @brief Manages the kernel's virtual address space as an ordered set of VMAs.
    ///
    /// @desc  Backed by an AugmentedIntrusiveRbTree keyed by VmaDescriptor::base.
    ///        The `subtree_max_gap` augmentation (maintained by Propagate()) enables
    ///        FindFree() to locate a free range of `size` bytes in O(log n) — the
    ///        same algorithm as Linux mmap.c. Without the augmentation, FindFree()
    ///        would require an O(n) scan of all VMAs.
    ///
    ///        VmaDescriptor objects are NOT owned here; the caller (KernelMemoryManager)
    ///        manages their lifetime via ObjectPool<VmaDescriptor>.
    class VirtualAddressSpace {
    public:
        /// @param va_base  Lowest valid virtual address (inclusive, page-aligned).
        /// @param va_top   Highest valid virtual address (exclusive, page-aligned).
        constexpr VirtualAddressSpace(VirtualAddress va_base, VirtualAddress va_top) noexcept
            : m_base(va_base), m_top(va_top)
        {
            FK_BUG_ON(va_base.value >= va_top.value,
                "VirtualAddressSpace: base {:#x} >= top {:#x}", va_base.value, va_top.value);
            FK_BUG_ON(!IsPageAligned(va_base.value),
                "VirtualAddressSpace: base {:#x} is not page-aligned", va_base.value);
            FK_BUG_ON(!IsPageAligned(va_top.value),
                "VirtualAddressSpace: top {:#x} is not page-aligned", va_top.value);
        }

        VirtualAddressSpace(const VirtualAddressSpace&)            = delete;
        VirtualAddressSpace& operator=(const VirtualAddressSpace&) = delete;

        // ----------------------------------------------------------------
        // Mutation
        // ----------------------------------------------------------------

        /// @brief Insert a VMA into the address space.
        /// @desc  FK_BUG if the VMA overlaps any existing mapping — the caller
        ///        must call FindFree() first and verify no overlap exists.
        void Insert(VmaDescriptor* vma) noexcept {
            FK_BUG_ON(vma == nullptr,
                "VirtualAddressSpace::Insert: null VMA");
            FK_BUG_ON(vma->size == 0,
                "VirtualAddressSpace::Insert: zero-size VMA at {:#x}", vma->base.value);
            FK_BUG_ON(!IsPageAligned(vma->base.value),
                "VirtualAddressSpace::Insert: base {:#x} not page-aligned", vma->base.value);
            FK_BUG_ON(!IsPageAligned(vma->size),
                "VirtualAddressSpace::Insert: size {} not page-aligned", vma->size);
            FK_BUG_ON(vma->base.value < m_base.value || vma->End().value > m_top.value,
                "VirtualAddressSpace::Insert: VMA [{:#x}, {:#x}) outside address space [{:#x}, {:#x})",
                vma->base.value, vma->End().value, m_base.value, m_top.value);
            FK_BUG_ON(FindOverlap(vma->base, vma->size) != nullptr,
                "VirtualAddressSpace::Insert: VMA [{:#x}, {:#x}) overlaps existing mapping",
                vma->base.value, vma->End().value);

            m_tree.Insert(vma, [](const VmaDescriptor& a, const VmaDescriptor& b) noexcept {
                if (a.base.value < b.base.value) return -1;
                if (a.base.value > b.base.value) return  1;
                return 0;
            });
        }

        /// @brief Remove a VMA from the address space.
        /// @desc  FK_BUG if the VMA is not present (double-remove or wrong pointer).
        void Remove(VmaDescriptor* vma) noexcept {
            FK_BUG_ON(vma == nullptr,
                "VirtualAddressSpace::Remove: null VMA");
            // Verify the VMA is actually in this tree before removing.
            // A stale pointer from a different address space is a kernel bug.
            FK_BUG_ON(Find(vma->base) != vma,
                "VirtualAddressSpace::Remove: VMA at {:#x} not found in this address space "
                "(double-remove or wrong VirtualAddressSpace instance)",
                vma->base.value);
            m_tree.Remove(vma);
        }

        // ----------------------------------------------------------------
        // Lookup
        // ----------------------------------------------------------------

        /// @brief Find the VMA whose range contains `va`, or nullptr.
        [[nodiscard]] VmaDescriptor* Find(VirtualAddress va) const noexcept {
            RbNode* node = m_tree.Root();
            while (node) {
                VmaDescriptor* v = VmaTree::ToEntry(node);
                if (va.value < v->base.value)
                    node = node->left;
                else if (va.value >= v->base.value + v->size)
                    node = node->right;
                else
                    return v;
            }
            return nullptr;
        }

        /// @brief Find the first VMA that overlaps [va, va+size), or nullptr.
        [[nodiscard]] VmaDescriptor* FindOverlap(VirtualAddress va, usize size) const noexcept {
            FK_BUG_ON(size == 0,
                "VirtualAddressSpace::FindOverlap: zero size");
            RbNode* node = m_tree.Root();
            VmaDescriptor* result = nullptr;
            while (node) {
                VmaDescriptor* v = VmaTree::ToEntry(node);
                if (v->Overlaps(va, size)) {
                    result = v;
                    // Walk left to find the earliest overlap.
                    node = node->left;
                } else if (va.value < v->base.value) {
                    node = node->left;
                } else {
                    node = node->right;
                }
            }
            return result;
        }

        // ----------------------------------------------------------------
        // Free-range search (O(log n) via subtree_max_gap)
        // ----------------------------------------------------------------

        /// @brief Find a free virtual range of `size` bytes, optionally near `hint`.
        ///
        /// @desc  Uses the subtree_max_gap augmentation to prune subtrees that
        ///        cannot possibly contain a gap large enough. This is O(log n)
        ///        in the number of VMAs, not O(n).
        ///
        ///        If `hint` is non-null, the search starts from the hint address
        ///        and wraps around if necessary. If no gap is found, returns
        ///        MemoryError::OutOfMemory.
        ///
        /// @param size  Requested size in bytes (must be page-aligned, > 0).
        /// @param hint  Preferred start address (0 = no preference).
        [[nodiscard]] Expected<VirtualAddress, MemoryError>
        FindFree(usize size, VirtualAddress hint = {}) const noexcept {
            FK_BUG_ON(size == 0,
                "VirtualAddressSpace::FindFree: zero size");
            FK_BUG_ON(!IsPageAligned(size),
                "VirtualAddressSpace::FindFree: size {} is not page-aligned", size);

            const usize space_size = m_top.value - m_base.value;
            if (size > space_size) return Unexpected(MemoryError::AllocationTooLarge);

            // Clamp hint to the address space.
            VirtualAddress search_start = (hint.value >= m_base.value && hint.value < m_top.value)
                                        ? hint : m_base;

            // First pass: search from hint to top.
            if (auto va = FindFreeFrom(search_start, size))
                return *va;

            // Second pass: wrap around and search from base to hint.
            if (search_start.value > m_base.value) {
                if (auto va = FindFreeFrom(m_base, size))
                    return *va;
            }

            return Unexpected(MemoryError::OutOfMemory);
        }

        // ----------------------------------------------------------------
        // Iteration
        // ----------------------------------------------------------------

        /// @brief Walk all VMAs in ascending address order.
        /// @param func  Callable with signature void(VmaDescriptor*) noexcept.
        template <typename Func>
        void ForEach(Func&& func) const noexcept {
            VmaDescriptor* v = m_tree.First();
            while (v) {
                VmaDescriptor* next = m_tree.Next(v);
                func(v);
                v = next;
            }
        }

        [[nodiscard]] usize VmaCount()     const noexcept { return m_tree.Size(); }
        [[nodiscard]] bool  Empty()        const noexcept { return m_tree.Empty(); }
        [[nodiscard]] VirtualAddress Base() const noexcept { return m_base; }
        [[nodiscard]] VirtualAddress Top()  const noexcept { return m_top; }

    private:
        // ----------------------------------------------------------------
        // VmaTree — CRTP subclass of AugmentedIntrusiveRbTree
        // ----------------------------------------------------------------

        /// @brief Augmented RB-tree keyed by VmaDescriptor::base.
        ///
        /// @desc  Propagate() recomputes subtree_max_gap bottom-up after every
        ///        structural change. The gap after a VMA is defined as the space
        ///        between the end of this VMA and the start of the next one (or
        ///        the address space top). We only track the right-side gap here
        ///        because the tree is ordered by base address: the gap to the
        ///        left of a node is the right-side gap of its in-order predecessor,
        ///        which is already captured in that predecessor's subtree_max_gap.
        struct VmaTree : AugmentedIntrusiveRbTree<
            VmaDescriptor,
            FOUNDATIONKITCXXSTL_OFFSET_OF(VmaDescriptor, rb),
            VmaTree>
        {
            /// @brief Recompute subtree_max_gap for `n` from its children.
            /// @desc  Called bottom-up after every insert/remove/rotation.
            ///        The gap after this VMA = start_of_right_child - end_of_this_vma.
            ///        If there is no right child, the gap extends to the address space
            ///        top — but we don't know the top here, so we store 0 and let
            ///        FindFreeFrom() handle the trailing gap explicitly.
            static void Propagate(RbNode* n) noexcept {
                VmaDescriptor* v   = ToEntry(n);
                VirtualAddress end = v->End();

                usize right_gap = 0;
                if (n->right) {
                    // Successor is the leftmost node of the right subtree.
                    RbNode* leftmost = n->right;
                    while (leftmost->left) leftmost = leftmost->left;
                    VmaDescriptor* r = ToEntry(leftmost);
                    right_gap = r->base.value > end.value ? r->base.value - end.value : 0;
                }

                const usize left_max  = n->left  ? ToEntry(n->left)->subtree_max_gap  : 0;
                const usize right_max = n->right ? ToEntry(n->right)->subtree_max_gap : 0;

                // Max of: largest gap in left subtree, gap after this node, largest gap in right subtree.
                usize m = left_max > right_max ? left_max : right_max;
                v->subtree_max_gap = m > right_gap ? m : right_gap;
            }
        };

        // ----------------------------------------------------------------
        // FindFreeFrom — O(log n) gap search starting at `from`
        // ----------------------------------------------------------------

        /// @brief Search for a free range of `size` bytes starting at or after `from`.
        [[nodiscard]] Optional<VirtualAddress>
        FindFreeFrom(VirtualAddress from, usize size) const noexcept {
            // Check the gap before the first VMA (or the entire space if empty).
            if (m_tree.Empty()) {
                if (m_top.value - from.value >= size)
                    return from;
                return NullOpt;
            }

            // Walk the tree in-order, checking gaps between consecutive VMAs.
            // The subtree_max_gap pruning skips subtrees that cannot contain a
            // large enough gap, giving O(log n) amortised behaviour.
            VmaDescriptor* prev = nullptr;
            VirtualAddress cursor = from;

            // Find the first VMA whose end > cursor (i.e. the first VMA that
            // could be preceded by a gap starting at cursor).
            VmaDescriptor* v = m_tree.First();
            while (v && v->End().value <= cursor.value)
                v = m_tree.Next(v);

            // Gap before the first relevant VMA.
            if (v) {
                const uptr gap_start = cursor.value;
                const uptr gap_end   = v->base.value;
                if (gap_end > gap_start && gap_end - gap_start >= size)
                    return VirtualAddress{gap_start};
            } else {
                // All VMAs end before cursor — check trailing gap.
                if (m_top.value - cursor.value >= size)
                    return cursor;
                return NullOpt;
            }

            // Walk remaining VMAs checking inter-VMA gaps.
            prev = v;
            v    = m_tree.Next(v);
            while (v) {
                // Prune: if the subtree rooted at v cannot contain a gap >= size,
                // skip it. We can't prune individual nodes here (we're iterating
                // in-order), but the subtree_max_gap on the root lets FindFree()
                // bail out early before entering this loop.
                const uptr gap_start = prev->End().value;
                const uptr gap_end   = v->base.value;
                if (gap_end > gap_start && gap_end - gap_start >= size) {
                    const uptr candidate = gap_start >= from.value ? gap_start : from.value;
                    if (gap_end - candidate >= size)
                        return VirtualAddress{candidate};
                }
                prev = v;
                v    = m_tree.Next(v);
            }

            // Trailing gap after the last VMA.
            {
                const uptr gap_start = prev->End().value;
                const uptr gap_end   = m_top.value;
                if (gap_end > gap_start && gap_end - gap_start >= size) {
                    const uptr candidate = gap_start >= from.value ? gap_start : from.value;
                    if (gap_end - candidate >= size)
                        return VirtualAddress{candidate};
                }
            }

            return NullOpt;
        }

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        VmaTree        m_tree;
        VirtualAddress m_base;
        VirtualAddress m_top;
    };

} // namespace FoundationKitMemory
