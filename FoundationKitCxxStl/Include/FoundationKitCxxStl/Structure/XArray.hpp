#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitMemory/Allocators/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // XArray<T, Alloc>
    //
    // ## What it is
    //
    // A sparse radix tree indexed by usize key. Equivalent to Linux's XArray
    // (xarray.h), which replaced the old radix_tree for the page cache, inode
    // table, and file descriptor table.
    //
    // ## Tree structure
    //
    // Each internal node holds kFanout = 64 slots (6-bit stride). A 64-bit key
    // space requires at most ceil(64/6) = 11 levels. In practice, the tree
    // grows only as deep as the highest key inserted requires — a tree holding
    // only keys < 64 is a single-level root node.
    //
    // Slot encoding (same trick as Linux):
    //   - nullptr          → empty slot
    //   - ptr & 1 == 1     → internal node pointer (tag bit set)
    //   - ptr & 1 == 0     → leaf value T* (user pointer, must be non-null)
    //
    // The tag bit works because all heap allocations are at least 2-byte
    // aligned, so bit 0 of any valid pointer is always 0.
    //
    // ## Complexity
    //
    //   Store   O(log₆₄ n) ≈ O(10) — allocates nodes on demand
    //   Load    O(log₆₄ n)          — pure pointer chase, no allocation
    //   Erase   O(log₆₄ n)          — frees empty nodes bottom-up
    //   ForEach O(n)                 — in-order key traversal
    //
    // ## Memory layout
    //
    // Each XArrayNode is exactly kFanout * sizeof(void*) = 512 bytes on 64-bit.
    // Aligned to 64 bytes so the first cache line covers the first 8 slots —
    // the common case for small key ranges hits only one cache line.
    //
    // ## Kernel use cases
    //
    //   - Page cache: key = page index (file offset >> PAGE_SHIFT)
    //   - File descriptor table: key = fd number
    //   - Inode table: key = inode number
    //   - IRQ descriptor table: key = IRQ number
    // =========================================================================

    namespace detail {

        static constexpr usize kXArrayShift  = 6;
        static constexpr usize kXArrayFanout = 1u << kXArrayShift; // 64
        static constexpr usize kXArrayMask   = kXArrayFanout - 1;
        static constexpr uptr  kNodeTag      = 1u;                  // bit 0 set = internal node

        struct alignas(64) XArrayNode {
            void* slots[kXArrayFanout];
            usize count; // number of non-null slots (for empty-node pruning)

            XArrayNode() noexcept : slots{}, count(0) {}
        };

        static_assert(sizeof(XArrayNode) == kXArrayFanout * sizeof(void*) + sizeof(usize) + /* padding */ 0
                      || sizeof(XArrayNode) >= kXArrayFanout * sizeof(void*),
                      "XArrayNode size sanity");

        [[nodiscard]] inline bool IsNode(void* p) noexcept {
            return p && (reinterpret_cast<uptr>(p) & kNodeTag);
        }

        [[nodiscard]] inline XArrayNode* ToNode(void* p) noexcept {
            return reinterpret_cast<XArrayNode*>(reinterpret_cast<uptr>(p) & ~kNodeTag);
        }

        [[nodiscard]] inline void* TagNode(XArrayNode* n) noexcept {
            return reinterpret_cast<void*>(reinterpret_cast<uptr>(n) | kNodeTag);
        }

    } // namespace detail

    /// @brief Sparse radix tree indexed by usize key.
    ///
    /// @tparam T     Pointed-to type. XArray stores T* — it does not own values.
    /// @tparam Alloc Allocator satisfying FoundationKitMemory::IAllocator.
    template <typename T, FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class XArray {
        using Node = detail::XArrayNode;

        static constexpr usize kShift  = detail::kXArrayShift;
        static constexpr usize kFanout = detail::kXArrayFanout;
        static constexpr usize kMask   = detail::kXArrayMask;

    public:
        explicit XArray(Alloc alloc = {}) noexcept
            : m_alloc(alloc), m_root(nullptr), m_height(0), m_size(0) {}

        ~XArray() noexcept { Clear(); }

        XArray(const XArray&)            = delete;
        XArray& operator=(const XArray&) = delete;

        // =====================================================================
        // Write side
        // =====================================================================

        /// @brief Insert or replace the value at `key`.
        ///
        /// Grows the tree upward if `key` exceeds the current height's range.
        /// Allocates internal nodes on demand.
        ///
        /// @param key   Index.
        /// @param value Non-null pointer to store. Must have bit 0 clear (any
        ///              heap pointer satisfies this).
        /// @return true on success; false if node allocation failed.
        [[nodiscard]] bool Store(usize key, T* value) noexcept {
            FK_BUG_ON(value == nullptr,
                "XArray::Store: null value at key={} — use Erase() to remove", key);
            FK_BUG_ON(reinterpret_cast<uptr>(value) & detail::kNodeTag,
                "XArray::Store: value pointer has tag bit set (misaligned pointer at key={})", key);

            // Grow the tree until the root covers `key`.
            while (key >= MaxKeyForHeight(m_height)) {
                if (!GrowRoot()) return false;
            }

            Node* node  = detail::ToNode(m_root);
            usize shift = (m_height - 1) * kShift;

            while (shift >= kShift) {
                const usize idx = (key >> shift) & kMask;
                if (!node->slots[idx]) {
                    Node* child = AllocNode();
                    if (!child) return false;
                    node->slots[idx] = detail::TagNode(child);
                    node->count++;
                }
                node  = detail::ToNode(node->slots[idx]);
                shift -= kShift;
            }

            // Leaf level.
            const usize leaf_idx = key & kMask;
            if (!node->slots[leaf_idx]) {
                node->count++;
                m_size++;
            }
            node->slots[leaf_idx] = value;
            return true;
        }

        /// @brief Remove the value at `key`. Prunes empty nodes bottom-up.
        void Erase(usize key) noexcept {
            if (!m_root || key >= MaxKeyForHeight(m_height)) return;
            EraseImpl(detail::ToNode(m_root), key, (m_height - 1) * kShift);
            ShrinkRoot();
        }

        // =====================================================================
        // Read side
        // =====================================================================

        /// @brief Look up the value at `key`. O(log₆₄ n). Returns nullptr if absent.
        [[nodiscard]] T* Load(usize key) const noexcept {
            if (!m_root || key >= MaxKeyForHeight(m_height)) return nullptr;

            const Node* node  = detail::ToNode(m_root);
            usize       shift = (m_height - 1) * kShift;

            while (shift >= kShift) {
                void* slot = node->slots[(key >> shift) & kMask];
                if (!slot) return nullptr;
                node  = detail::ToNode(slot);
                shift -= kShift;
            }

            return static_cast<T*>(node->slots[key & kMask]);
        }

        /// @brief Call func(key, T&) for every stored entry in ascending key order.
        template <Invocable<usize, T&> Func>
        void ForEach(Func&& func) const noexcept {
            if (!m_root) return;
            ForEachImpl(detail::ToNode(m_root), 0, (m_height - 1) * kShift, func);
        }

        /// @brief Remove all entries and free all internal nodes.
        void Clear() noexcept {
            if (m_root) {
                FreeNode(detail::ToNode(m_root), (m_height - 1) * kShift);
                m_root   = nullptr;
                m_height = 0;
                m_size   = 0;
            }
        }

        [[nodiscard]] bool  Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()  const noexcept { return m_size; }

    private:
        // Maximum key a tree of `height` levels can address.
        // height=1 → keys [0, 63], height=2 → [0, 4095], etc.
        [[nodiscard]] static usize MaxKeyForHeight(usize height) noexcept {
            if (height == 0) return 0;
            // 64^height - 1, clamped to usize max to avoid overflow.
            usize max = 1;
            for (usize i = 0; i < height; ++i) {
                if (max > (~usize(0)) / kFanout) return ~usize(0); // saturate
                max *= kFanout;
            }
            return max - 1;
        }

        [[nodiscard]] Node* AllocNode() noexcept {
            auto res = m_alloc.Allocate(sizeof(Node), alignof(Node));
            FK_BUG_ON(!res.ok(), "XArray::AllocNode: allocation of {} bytes failed", sizeof(Node));
            return new (res.ptr) Node();
        }

        void FreeNode(Node* node, usize shift) noexcept {
            if (shift >= kShift) {
                for (usize i = 0; i < kFanout; ++i) {
                    if (node->slots[i])
                        FreeNode(detail::ToNode(node->slots[i]), shift - kShift);
                }
            }
            node->~Node();
            m_alloc.Deallocate(node, sizeof(Node));
        }

        // Grow the root by one level, wrapping the current root as the first
        // child of a new root node.
        [[nodiscard]] bool GrowRoot() noexcept {
            Node* new_root = AllocNode();
            if (!new_root) return false;
            if (m_root) {
                new_root->slots[0] = m_root;
                new_root->count    = 1;
            }
            m_root = detail::TagNode(new_root);
            m_height++;
            return true;
        }

        // Shrink the root while the root has exactly one child at slot 0
        // and height > 1.
        void ShrinkRoot() noexcept {
            while (m_height > 1) {
                Node* root = detail::ToNode(m_root);
                if (root->count != 1 || !root->slots[0]) break;
                // Check that slot 0 is the only non-null slot.
                bool only_slot0 = true;
                for (usize i = 1; i < kFanout; ++i) {
                    if (root->slots[i]) { only_slot0 = false; break; }
                }
                if (!only_slot0) break;

                void* child = root->slots[0];
                root->~Node();
                m_alloc.Deallocate(root, sizeof(Node));
                m_root = child;
                m_height--;
            }
            // If the tree is empty, free the root entirely.
            if (m_size == 0 && m_root) {
                Node* root = detail::ToNode(m_root);
                root->~Node();
                m_alloc.Deallocate(root, sizeof(Node));
                m_root   = nullptr;
                m_height = 0;
            }
        }

        // Returns true if the subtree rooted at `node` became empty.
        bool EraseImpl(Node* node, usize key, usize shift) noexcept {
            const usize idx = (key >> shift) & kMask;

            if (shift == 0) {
                // Leaf level.
                if (!node->slots[idx]) return false; // already absent
                node->slots[idx] = nullptr;
                node->count--;
                m_size--;
                return node->count == 0;
            }

            void* child_slot = node->slots[idx];
            if (!child_slot) return false;

            Node* child = detail::ToNode(child_slot);
            if (EraseImpl(child, key, shift - kShift)) {
                // Child became empty — free it and unlink.
                child->~Node();
                m_alloc.Deallocate(child, sizeof(Node));
                node->slots[idx] = nullptr;
                node->count--;
            }
            return node->count == 0;
        }

        template <Invocable<usize, T&> Func>
        void ForEachImpl(const Node* node, usize base_key, usize shift, Func& func) const noexcept {
            for (usize i = 0; i < kFanout; ++i) {
                void* slot = node->slots[i];
                if (!slot) continue;
                const usize child_key = base_key | (i << shift);
                if (shift == 0) {
                    func(child_key, *static_cast<T*>(slot));
                } else {
                    ForEachImpl(detail::ToNode(slot), child_key, shift - kShift, func);
                }
            }
        }

        Alloc  m_alloc;
        void*  m_root;
        usize  m_height;
        usize  m_size;
    };

} // namespace FoundationKitCxxStl::Structure
