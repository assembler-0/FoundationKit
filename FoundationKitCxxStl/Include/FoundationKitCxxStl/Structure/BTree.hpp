#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitMemory/Allocators/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // BTree<Key, T, Order, Compare, Alloc>
    //
    // A B+ tree: all values live at leaf nodes; internal nodes hold only keys
    // and child pointers. Leaves are linked in a doubly-linked list for O(n)
    // ordered range scans without tree traversal.
    //
    // ## Fan-out (Order)
    //
    // Each internal node holds [Order-1, 2*Order-1] keys and [Order, 2*Order]
    // children. Each leaf holds [Order-1, 2*Order-1] key-value pairs.
    // Order=32 gives ~512-byte nodes on 64-bit — fits in 8 cache lines.
    //
    // ## Overflow slots
    //
    // Node arrays are sized kMaxKeys+1 (and kMaxChildren+1 for internal nodes).
    // InsertImpl writes the new entry into the node BEFORE checking whether a
    // split is needed. Without the extra slot, that write goes one past the end
    // of the array and corrupts the adjacent prev/next/children pointers.
    // The extra slot is never visible to callers — count never exceeds kMaxKeys
    // after a split.
    //
    // ## Kernel use cases
    //
    //   - Extent map: key = file offset, value = disk block pointer
    //   - Page table (software-managed): key = virtual page, value = PTE*
    //   - Process table: key = PID, value = task_struct*
    //
    // ## Complexity
    //
    //   Find        O(log_Order n)
    //   Insert      O(log_Order n)  — may split O(log n) nodes
    //   Erase       O(log_Order n)  — may merge/rebalance O(log n) nodes
    //   RangeScan   O(log n + k)    — k = number of results
    //   ForEach     O(n)
    // =========================================================================

    namespace detail {

        template <bool B, typename X, typename Y> struct BTreeCond           { using Type = X; };
        template <typename X, typename Y>          struct BTreeCond<false,X,Y>{ using Type = Y; };
        template <bool B, typename X, typename Y>
        using BTreeConditional = BTreeCond<B, X, Y>::Type;

        struct BTreeDefaultLess {
            template <typename K>
            bool operator()(const K& a, const K& b) const noexcept { return a < b; }
        };

        template <typename Compare>
        using BTreeCmp = BTreeConditional<SameAs<Compare, void>, BTreeDefaultLess, Compare>;

        static constexpr u8 kBTreeLeafTag     = 0;
        static constexpr u8 kBTreeInternalTag = 1;

        template <typename Key, typename T, usize Order>
        struct BTreeNodeBase {
            u8    tag;
            usize count;
            [[nodiscard]] bool IsLeaf()     const noexcept { return tag == kBTreeLeafTag; }
            [[nodiscard]] bool IsInternal() const noexcept { return tag == kBTreeInternalTag; }
        };

        template <typename Key, typename T, usize Order>
        struct BTreeLeaf : BTreeNodeBase<Key, T, Order> {
            static constexpr usize kMaxKeys = 2 * Order - 1;
            // +1: temporary overflow slot used during insert before split fires.
            Key        keys[kMaxKeys + 1];
            T*         values[kMaxKeys + 1];
            BTreeLeaf* prev;
            BTreeLeaf* next;

            BTreeLeaf() noexcept {
                this->tag   = kBTreeLeafTag;
                this->count = 0;
                prev = next = nullptr;
            }
        };

        template <typename Key, typename T, usize Order>
        struct BTreeInternal : BTreeNodeBase<Key, T, Order> {
            static constexpr usize kMaxKeys     = 2 * Order - 1;
            static constexpr usize kMaxChildren = 2 * Order;
            using NodeBase = BTreeNodeBase<Key, T, Order>;

            // +1 overflow slots for the same reason as BTreeLeaf.
            Key       keys[kMaxKeys + 1];
            NodeBase* children[kMaxChildren + 1];

            BTreeInternal() noexcept {
                this->tag   = kBTreeInternalTag;
                this->count = 0;
                for (usize i = 0; i < kMaxChildren + 1; ++i) children[i] = nullptr;
            }
        };

    } // namespace detail

    /// @brief B+ tree mapping Key → T*.
    ///
    /// Stores non-owning T* pointers. The caller owns the pointed-to objects.
    ///
    /// @tparam Key     Key type. Must be TotallyOrdered (or supply Compare).
    /// @tparam T       Value type. BTree stores T* — it does not own values.
    /// @tparam Order   B-tree order. Each node holds [Order-1, 2*Order-1] keys.
    /// @tparam Compare Strict-weak-ordering functor, or void to use operator<.
    /// @tparam Alloc   Allocator satisfying FoundationKitMemory::IAllocator.
    template <
        typename Key,
        typename T,
        usize Order = 32,
        typename Compare = void,
        FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator
    >
    class BTree {
        static_assert(Order >= 2, "BTree: Order must be >= 2");

        using Leaf     = detail::BTreeLeaf<Key, T, Order>;
        using Internal = detail::BTreeInternal<Key, T, Order>;
        using NodeBase = detail::BTreeNodeBase<Key, T, Order>;
        using Cmp      = detail::BTreeCmp<Compare>;

        static constexpr usize kMaxKeys     = 2 * Order - 1;
        static constexpr usize kMinKeys     = Order - 1;
        static constexpr usize kMaxChildren = 2 * Order;

    public:
        explicit BTree(Alloc alloc = {}) noexcept
            : m_alloc(alloc), m_root(nullptr), m_first_leaf(nullptr), m_size(0) {}

        ~BTree() noexcept { Clear(); }

        BTree(const BTree&)            = delete;
        BTree& operator=(const BTree&) = delete;

        // =====================================================================
        // Write side
        // =====================================================================

        /// @brief Insert key → value. If key already exists, updates the value.
        /// @return true on success; false if node allocation failed.
        [[nodiscard]] bool Insert(const Key& key, T* value) noexcept {
            FK_BUG_ON(value == nullptr,
                "BTree::Insert: null value — use Erase() to remove a key");

            if (!m_root) {
                Leaf* leaf = AllocLeaf();
                if (!leaf) return false;
                leaf->keys[0]   = key;
                leaf->values[0] = value;
                leaf->count     = 1;
                m_root       = leaf;
                m_first_leaf = leaf;
                m_size++;
                return true;
            }

            Key       split_key{};
            NodeBase* split_right = nullptr;
            if (!InsertImpl(m_root, key, value, split_key, split_right))
                return false;

            if (split_right) {
                Internal* new_root = AllocInternal();
                if (!new_root) return false;
                new_root->keys[0]     = split_key;
                new_root->children[0] = m_root;
                new_root->children[1] = split_right;
                new_root->count       = 1;
                m_root = new_root;
            }
            return true;
        }

        /// @brief Remove the entry with `key`. O(log n).
        /// @return true if the key was present and removed.
        bool Erase(const Key& key) noexcept {
            if (!m_root) return false;
            const bool removed = EraseImpl(m_root, nullptr, 0, key);
            if (removed) {
                m_size--;
                // Collapse internal root nodes reduced to a single child by merges.
                // Loop: a merge cascade can shrink multiple levels in one Erase.
                while (!m_root->IsLeaf() && m_root->count == 0) {
                    Internal* old = static_cast<Internal*>(m_root);
                    m_root = old->children[0];
                    old->~Internal();
                    m_alloc.Deallocate(old, sizeof(Internal));
                }
                // Empty leaf root: tree is now empty. Merges only free the right
                // sibling, so the surviving leaf is never freed inside EraseImpl.
                if (m_root->IsLeaf() && m_root->count == 0) {
                    Leaf* leaf = static_cast<Leaf*>(m_root);
                    leaf->~Leaf();
                    m_alloc.Deallocate(leaf, sizeof(Leaf));
                    m_root       = nullptr;
                    m_first_leaf = nullptr;
                }
            }
            return removed;
        }

        // =====================================================================
        // Read side
        // =====================================================================

        /// @brief Find the value for `key`. O(log_Order n). Returns nullptr if absent.
        [[nodiscard]] T* Find(const Key& key) const noexcept {
            const NodeBase* node = m_root;
            while (node && !node->IsLeaf())
                node = static_cast<const Internal*>(node)
                           ->children[UpperBound(static_cast<const Internal*>(node), key)];
            if (!node) return nullptr;
            const Leaf* leaf = static_cast<const Leaf*>(node);
            const usize idx  = LowerBound(leaf, key);
            if (idx < leaf->count &&
                !Cmp{}(key, leaf->keys[idx]) && !Cmp{}(leaf->keys[idx], key))
                return leaf->values[idx];
            return nullptr;
        }

        /// @brief Call func(key, T&) for all entries with key in [lo, hi]. O(log n + k).
        template <typename Func>
        void RangeScan(const Key& lo, const Key& hi, Func&& func) const noexcept {
            const NodeBase* node = m_root;
            while (node && !node->IsLeaf())
                node = static_cast<const Internal*>(node)
                           ->children[UpperBound(static_cast<const Internal*>(node), lo)];
            const Leaf* leaf = static_cast<const Leaf*>(node);
            while (leaf) {
                for (usize i = 0; i < leaf->count; ++i) {
                    if (Cmp{}(hi, leaf->keys[i])) return;
                    if (!Cmp{}(leaf->keys[i], lo))
                        func(leaf->keys[i], *leaf->values[i]);
                }
                leaf = leaf->next;
            }
        }

        /// @brief Call func(key, T&) for every entry in ascending key order. O(n).
        template <typename Func>
        void ForEach(Func&& func) const noexcept {
            const Leaf* leaf = m_first_leaf;
            while (leaf) {
                for (usize i = 0; i < leaf->count; ++i)
                    func(leaf->keys[i], *leaf->values[i]);
                leaf = leaf->next;
            }
        }

        void Clear() noexcept {
            if (m_root) {
                FreeNode(m_root);
                m_root       = nullptr;
                m_first_leaf = nullptr;
                m_size       = 0;
            }
        }

        [[nodiscard]] bool  Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()  const noexcept { return m_size; }

    private:
        static usize LowerBound(const Leaf* leaf, const Key& key) noexcept {
            usize lo = 0, hi = leaf->count;
            while (lo < hi) {
                const usize mid = lo + (hi - lo) / 2;
                if (Cmp{}(leaf->keys[mid], key)) lo = mid + 1;
                else                              hi = mid;
            }
            return lo;
        }

        static usize UpperBound(const Internal* node, const Key& key) noexcept {
            usize lo = 0, hi = node->count;
            while (lo < hi) {
                const usize mid = lo + (hi - lo) / 2;
                if (!Cmp{}(key, node->keys[mid])) lo = mid + 1;
                else                               hi = mid;
            }
            return lo;
        }

        bool InsertImpl(NodeBase* node, const Key& key, T* value,
                        Key& split_key, NodeBase*& split_right) noexcept {
            split_right = nullptr;

            if (node->IsLeaf()) {
                Leaf* leaf = static_cast<Leaf*>(node);
                usize idx  = LowerBound(leaf, key);

                // Update existing key — no size change.
                if (idx < leaf->count &&
                    !Cmp{}(key, leaf->keys[idx]) && !Cmp{}(leaf->keys[idx], key)) {
                    leaf->values[idx] = value;
                    return true;
                }

                // Shift right into the overflow slot, then write.
                // The overflow slot (index kMaxKeys) is safe because the array
                // is sized kMaxKeys+1.
                for (usize i = leaf->count; i > idx; --i) {
                    leaf->keys[i]   = leaf->keys[i - 1];
                    leaf->values[i] = leaf->values[i - 1];
                }
                leaf->keys[idx]   = key;
                leaf->values[idx] = value;
                leaf->count++;
                m_size++;

                if (leaf->count == kMaxKeys + 1) {
                    Leaf* right = AllocLeaf();
                    if (!right) return false;
                    SplitLeaf(leaf, right, split_key);
                    split_right = right;
                }
                return true;
            }

            Internal*   internal  = static_cast<Internal*>(node);
            const usize child_idx = UpperBound(internal, key);

            Key       child_split_key{};
            NodeBase* child_split_right = nullptr;
            if (!InsertImpl(internal->children[child_idx], key, value,
                            child_split_key, child_split_right))
                return false;

            if (!child_split_right) return true;

            // Shift keys/children right into the overflow slot.
            for (usize i = internal->count; i > child_idx; --i) {
                internal->keys[i]         = internal->keys[i - 1];
                internal->children[i + 1] = internal->children[i];
            }
            internal->keys[child_idx]         = child_split_key;
            internal->children[child_idx + 1] = child_split_right;
            internal->count++;

            if (internal->count == kMaxKeys + 1) {
                Internal* right = AllocInternal();
                if (!right) return false;
                SplitInternal(internal, right, split_key);
                split_right = right;
            }
            return true;
        }

        void SplitLeaf(Leaf* left, Leaf* right, Key& median) noexcept {
            // left->count == kMaxKeys+1 at this point (overflow slot is filled).
            // mid = Order: left keeps [0..Order-1] (Order keys = kMinKeys+1),
            //              right gets [Order..kMaxKeys] (Order keys).
            // Both halves satisfy the minimum occupancy invariant.
            const usize mid = Order;
            median = left->keys[mid]; // B+ tree: median copied up AND stays in right
            right->count = left->count - mid;
            for (usize i = 0; i < right->count; ++i) {
                right->keys[i]   = left->keys[mid + i];
                right->values[i] = left->values[mid + i];
            }
            left->count = mid;
            right->next = left->next;
            right->prev = left;
            if (left->next) left->next->prev = right;
            left->next  = right;
        }

        void SplitInternal(Internal* left, Internal* right, Key& median) noexcept {
            // left->count == kMaxKeys+1 at this point.
            // mid = Order-1: the key at mid is pushed up (not kept in either child).
            // left keeps [0..mid-1] (Order-1 keys), right gets [mid+1..kMaxKeys] (Order-1 keys).
            const usize mid = Order - 1;
            median = left->keys[mid];
            right->count = left->count - mid - 1;
            for (usize i = 0; i < right->count; ++i)
                right->keys[i] = left->keys[mid + 1 + i];
            for (usize i = 0; i <= right->count; ++i)
                right->children[i] = left->children[mid + 1 + i];
            left->count = mid;
        }

        bool EraseImpl(NodeBase* node, Internal* parent, usize parent_idx,
                       const Key& key) noexcept {
            if (node->IsLeaf()) {
                Leaf*       leaf = static_cast<Leaf*>(node);
                const usize idx  = LowerBound(leaf, key);
                if (idx >= leaf->count ||
                    Cmp{}(key, leaf->keys[idx]) || Cmp{}(leaf->keys[idx], key))
                    return false;

                for (usize i = idx; i + 1 < leaf->count; ++i) {
                    leaf->keys[i]   = leaf->keys[i + 1];
                    leaf->values[i] = leaf->values[i + 1];
                }
                --leaf->count;
                if (parent && leaf->count < kMinKeys)
                    RebalanceLeaf(leaf, parent, parent_idx);
                return true;
            }

            Internal*   internal = static_cast<Internal*>(node);
            const usize child_i  = UpperBound(internal, key);
            const bool  removed  = EraseImpl(internal->children[child_i],
                                             internal, child_i, key);
            if (removed && parent && internal->count < kMinKeys)
                RebalanceInternal(internal, parent, parent_idx);
            return removed;
        }

        void RebalanceLeaf(Leaf* leaf, Internal* parent, usize idx) noexcept {
            if (idx + 1 <= parent->count) {
                Leaf* right = static_cast<Leaf*>(parent->children[idx + 1]);
                if (right->count > kMinKeys) {
                    leaf->keys[leaf->count]   = right->keys[0];
                    leaf->values[leaf->count] = right->values[0];
                    ++leaf->count;
                    for (usize i = 0; i + 1 < right->count; ++i) {
                        right->keys[i]   = right->keys[i + 1];
                        right->values[i] = right->values[i + 1];
                    }
                    --right->count;
                    parent->keys[idx] = right->keys[0];
                    return;
                }
            }
            if (idx > 0) {
                Leaf* left = static_cast<Leaf*>(parent->children[idx - 1]);
                if (left->count > kMinKeys) {
                    for (usize i = leaf->count; i > 0; --i) {
                        leaf->keys[i]   = leaf->keys[i - 1];
                        leaf->values[i] = leaf->values[i - 1];
                    }
                    leaf->keys[0]   = left->keys[left->count - 1];
                    leaf->values[0] = left->values[left->count - 1];
                    ++leaf->count;
                    --left->count;
                    parent->keys[idx - 1] = leaf->keys[0];
                    return;
                }
            }
            if (idx + 1 <= parent->count)
                MergeLeaves(leaf, static_cast<Leaf*>(parent->children[idx + 1]),
                            parent, idx);
            else
                MergeLeaves(static_cast<Leaf*>(parent->children[idx - 1]),
                            leaf, parent, idx - 1);
        }

        void MergeLeaves(Leaf* left, Leaf* right,
                         Internal* parent, usize sep_idx) noexcept {
            for (usize i = 0; i < right->count; ++i) {
                left->keys[left->count + i]   = right->keys[i];
                left->values[left->count + i] = right->values[i];
            }
            left->count += right->count;
            left->next = right->next;
            if (right->next) right->next->prev = left;
            for (usize i = sep_idx; i + 1 < parent->count; ++i) {
                parent->keys[i]         = parent->keys[i + 1];
                parent->children[i + 1] = parent->children[i + 2];
            }
            --parent->count;
            right->~Leaf();
            m_alloc.Deallocate(right, sizeof(Leaf));
        }

        void RebalanceInternal(Internal* node, Internal* parent, usize idx) noexcept {
            if (idx + 1 <= parent->count) {
                Internal* right = static_cast<Internal*>(parent->children[idx + 1]);
                if (right->count > kMinKeys) {
                    node->keys[node->count]         = parent->keys[idx];
                    node->children[node->count + 1] = right->children[0];
                    ++node->count;
                    parent->keys[idx] = right->keys[0];
                    for (usize i = 0; i + 1 < right->count; ++i) {
                        right->keys[i]     = right->keys[i + 1];
                        right->children[i] = right->children[i + 1];
                    }
                    right->children[right->count - 1] = right->children[right->count];
                    --right->count;
                    return;
                }
            }
            if (idx > 0) {
                Internal* left = static_cast<Internal*>(parent->children[idx - 1]);
                if (left->count > kMinKeys) {
                    for (usize i = node->count; i > 0; --i) {
                        node->keys[i]         = node->keys[i - 1];
                        node->children[i + 1] = node->children[i];
                    }
                    node->children[1] = node->children[0];
                    node->keys[0]     = parent->keys[idx - 1];
                    node->children[0] = left->children[left->count];
                    ++node->count;
                    parent->keys[idx - 1] = left->keys[left->count - 1];
                    --left->count;
                    return;
                }
            }
            if (idx + 1 <= parent->count)
                MergeInternals(node,
                               static_cast<Internal*>(parent->children[idx + 1]),
                               parent, idx);
            else
                MergeInternals(static_cast<Internal*>(parent->children[idx - 1]),
                               node, parent, idx - 1);
        }

        void MergeInternals(Internal* left, Internal* right,
                            Internal* parent, usize sep_idx) noexcept {
            left->keys[left->count] = parent->keys[sep_idx];
            ++left->count;
            for (usize i = 0; i < right->count; ++i) {
                left->keys[left->count + i]     = right->keys[i];
                left->children[left->count + i] = right->children[i];
            }
            left->children[left->count + right->count] = right->children[right->count];
            left->count += right->count;
            for (usize i = sep_idx; i + 1 < parent->count; ++i) {
                parent->keys[i]         = parent->keys[i + 1];
                parent->children[i + 1] = parent->children[i + 2];
            }
            --parent->count;
            right->~Internal();
            m_alloc.Deallocate(right, sizeof(Internal));
        }

        [[nodiscard]] Leaf* AllocLeaf() noexcept {
            auto res = m_alloc.Allocate(sizeof(Leaf), alignof(Leaf));
            FK_BUG_ON(!res.ok(),
                "BTree::AllocLeaf: allocation failed ({} bytes)", sizeof(Leaf));
            return new (res.ptr) Leaf();
        }

        [[nodiscard]] Internal* AllocInternal() noexcept {
            auto res = m_alloc.Allocate(sizeof(Internal), alignof(Internal));
            FK_BUG_ON(!res.ok(),
                "BTree::AllocInternal: allocation failed ({} bytes)", sizeof(Internal));
            return new (res.ptr) Internal();
        }

        void FreeNode(NodeBase* node) noexcept {
            if (node->IsLeaf()) {
                Leaf* leaf = static_cast<Leaf*>(node);
                leaf->~Leaf();
                m_alloc.Deallocate(leaf, sizeof(Leaf));
            } else {
                Internal* internal = static_cast<Internal*>(node);
                for (usize i = 0; i <= internal->count; ++i)
                    if (internal->children[i]) FreeNode(internal->children[i]);
                internal->~Internal();
                m_alloc.Deallocate(internal, sizeof(Internal));
            }
        }

        Alloc     m_alloc;
        NodeBase* m_root;
        Leaf*     m_first_leaf;
        usize     m_size;
    };

} // namespace FoundationKitCxxStl::Structure
