#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // IntrusiveSkipList<T, NodeOffset, MaxLevel, Compare>
    //
    // ## What it is
    //
    // A probabilistic ordered set with O(log n) expected insert, remove, and
    // find. Each element T embeds a SkipNode that holds a tower of forward
    // pointers up to MaxLevel levels high. No dynamic allocation — the tower
    // is a fixed-size array inside SkipNode, so the entire structure lives in
    // the objects themselves.
    //
    // ## Why a skip list over AVL/RB-tree in a kernel
    //
    // - Lock-free extensions are straightforward (Harris-style CAS on next[0]).
    // - Cache-friendly forward traversal: next[0] is a singly-linked list at
    //   the bottom level — sequential scan is a pointer chase with no branch.
    // - Simpler rebalancing: no rotations, no color bits, no height fields.
    //   The probabilistic balance is good enough for most kernel workloads.
    // - Useful when you need both O(log n) point lookup AND O(n) ordered scan
    //   (e.g., timer wheel overflow list, run-queue range queries).
    //
    // ## Level selection — deterministic LFSR
    //
    // Standard skip lists use rand() for level selection. That is banned here
    // (libc dependency, non-deterministic). Instead we use a 32-bit Galois LFSR
    // seeded with the node's own address. This gives a deterministic, uniform
    // distribution without any external state. The polynomial x^32+x^22+x^2+x+1
    // has a full period of 2^32-1.
    //
    // The level of a new node is the number of trailing 1-bits in the LFSR
    // output, clamped to [1, MaxLevel]. This matches the geometric distribution
    // that a fair coin flip would produce (p=0.5 per level).
    //
    // ## Sentinel head
    //
    // The list has a sentinel head node at level MaxLevel. Its forward pointers
    // are initialised to nullptr. The sentinel is embedded in the list object
    // itself — no allocation.
    //
    // ## Complexity
    //
    //   Find    O(log n) expected
    //   Insert  O(log n) expected
    //   Remove  O(log n) expected
    //   First   O(1)
    //   Next    O(1)
    //   ForEach O(n)
    // =========================================================================

    /// @brief Intrusive skip list node. Embed one in each element type T.
    ///
    /// @tparam MaxLevel Maximum tower height. Typical values: 16 (65K elements),
    ///                  24 (16M elements), 32 (4B elements).
    template <usize MaxLevel>
    struct SkipNode {
        static_assert(MaxLevel >= 1 && MaxLevel <= 32,
            "SkipNode: MaxLevel must be in [1, 32]");

        SkipNode* next[MaxLevel];
        u8        level; // actual height of this node's tower [1, MaxLevel]

        constexpr SkipNode() noexcept : next{}, level(1) {}
    };

    /// @brief Intrusive probabilistic skip list.
    ///
    /// @tparam T          Container type embedding a SkipNode<MaxLevel> member.
    /// @tparam NodeOffset Byte offset of SkipNode within T (use FOUNDATIONKITCXXSTL_OFFSET_OF).
    /// @tparam MaxLevel   Maximum tower height.
    /// @tparam Compare    Strict-weak-ordering functor: Compare(a, b) → bool (a < b).
    template <typename T, usize NodeOffset, usize MaxLevel, typename Compare>
    class IntrusiveSkipList {
        static_assert(NodeOffset < 65536,
            "IntrusiveSkipList: NodeOffset exceeds 65535 bytes — did you swap T and NodeOffset?");
        static_assert(MaxLevel >= 1 && MaxLevel <= 32,
            "IntrusiveSkipList: MaxLevel must be in [1, 32]");

        using Node = SkipNode<MaxLevel>;

    public:
        IntrusiveSkipList() noexcept : m_size(0) {
            for (usize i = 0; i < MaxLevel; ++i)
                m_head.next[i] = nullptr;
            m_head.level = static_cast<u8>(MaxLevel);
        }

        IntrusiveSkipList(const IntrusiveSkipList&)            = delete;
        IntrusiveSkipList& operator=(const IntrusiveSkipList&) = delete;

        // =====================================================================
        // Core operations
        // =====================================================================

        /// @brief Insert `entry` into the list. O(log n) expected.
        ///
        /// Duplicate keys are allowed — the new entry is inserted after all
        /// existing entries with the same key. If strict uniqueness is required,
        /// call Find() first.
        void Insert(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr, "IntrusiveSkipList::Insert: null entry");
            Node* node = ToNode(entry);
            FK_BUG_ON(node->next[0] != nullptr,
                "IntrusiveSkipList::Insert: node appears already linked (next[0] non-null)");

            // Collect update pointers: update[i] = last node at level i whose
            // next[i] must be patched to point to the new node.
            Node* update[MaxLevel];
            Node* cur = &m_head;

            for (usize i = MaxLevel; i-- > 0;) {
                while (cur->next[i] && Compare{}(*ToEntry(cur->next[i]), *entry))
                    cur = cur->next[i];
                update[i] = cur;
            }

            const u8 lvl = RandomLevel(entry);
            node->level  = lvl;

            for (usize i = 0; i < lvl; ++i) {
                node->next[i]     = update[i]->next[i];
                update[i]->next[i] = node;
            }
            m_size++;
        }

        /// @brief Remove `entry` from the list. O(log n) expected.
        void Remove(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr, "IntrusiveSkipList::Remove: null entry");
            FK_BUG_ON(m_size == 0,     "IntrusiveSkipList::Remove: list is empty");

            Node* node = ToNode(entry);
            Node* update[MaxLevel];
            Node* cur = &m_head;

            for (usize i = MaxLevel; i-- > 0;) {
                while (cur->next[i] && Compare{}(*ToEntry(cur->next[i]), *entry))
                    cur = cur->next[i];
                update[i] = cur;
            }

            // Verify the node is actually in the list at level 0.
            FK_BUG_ON(update[0]->next[0] != node,
                "IntrusiveSkipList::Remove: entry not found in list at level 0 "
                "(possible double-remove or wrong list)");

            for (usize i = 0; i < node->level; ++i) {
                FK_BUG_ON(update[i]->next[i] != node,
                    "IntrusiveSkipList::Remove: tower inconsistency at level {} "
                    "(update[i]->next[i] != node — list corruption)", i);
                update[i]->next[i] = node->next[i];
            }

            // Reset node so it can be re-inserted later.
            for (usize i = 0; i < node->level; ++i)
                node->next[i] = nullptr;

            m_size--;
        }

        /// @brief Find the first entry equal to `key`. O(log n) expected.
        ///
        /// @param key  Key to search for.
        /// @param comp comp(key, entry) → int: negative/zero/positive.
        template <typename Key, typename KeyCompare>
        [[nodiscard]] T* Find(const Key& key, KeyCompare&& comp) const noexcept {
            const Node* cur = &m_head;
            for (usize i = MaxLevel; i-- > 0;) {
                while (cur->next[i]) {
                    const int res = comp(key, *ToEntry(cur->next[i]));
                    if (res == 0) return ToEntry(cur->next[i]);
                    if (res <  0) break;
                    cur = cur->next[i];
                }
            }
            return nullptr;
        }

        /// @brief Find the first entry whose key is >= `key`. O(log n) expected.
        template <typename Key, typename KeyCompare>
        [[nodiscard]] T* LowerBound(const Key& key, KeyCompare&& comp) const noexcept {
            const Node* cur = &m_head;
            for (usize i = MaxLevel; i-- > 0;) {
                while (cur->next[i] && comp(key, *ToEntry(cur->next[i])) > 0)
                    cur = cur->next[i];
            }
            return cur->next[0] ? ToEntry(cur->next[0]) : nullptr;
        }

        /// @brief Call func(T&) for every entry in ascending order. O(n).
        template <typename Func>
        void ForEach(Func&& func) noexcept {
            Node* cur = m_head.next[0];
            while (cur) {
                func(*ToEntry(cur));
                cur = cur->next[0];
            }
        }

        [[nodiscard]] T*    First() const noexcept { return m_head.next[0] ? ToEntry(m_head.next[0]) : nullptr; }
        [[nodiscard]] T*    Next(T* entry) const noexcept { return ToEntry(ToNode(entry)->next[0]); }
        [[nodiscard]] bool  Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()  const noexcept { return m_size; }

    private:
        // =====================================================================
        // Pointer helpers
        // =====================================================================

        static T* ToEntry(Node* node) noexcept {
            if (!node) return nullptr;
            return reinterpret_cast<T*>(reinterpret_cast<uptr>(node) - NodeOffset);
        }

        static const T* ToEntry(const Node* node) noexcept {
            if (!node) return nullptr;
            return reinterpret_cast<const T*>(reinterpret_cast<uptr>(node) - NodeOffset);
        }

        static Node* ToNode(T* entry) noexcept {
            if (!entry) return nullptr;
            return reinterpret_cast<Node*>(reinterpret_cast<uptr>(entry) + NodeOffset);
        }

        static u8 RandomLevel(const T* entry) noexcept {
            u32 lfsr = static_cast<u32>(reinterpret_cast<uptr>(entry) ^ 0x9e3779b9u);
            if (lfsr == 0) lfsr = 1; // LFSR must not be zero

            // One step of the Galois LFSR.
            const u32 lsb = lfsr & 1u;
            lfsr >>= 1;
            if (lsb) lfsr ^= 0x80200003u; // taps for x^32+x^22+x^2+x+1

            // Count trailing 1-bits → level in [1, MaxLevel].
            u8 level = 1;
            u32 bits = lfsr;
            while ((bits & 1u) && level < static_cast<u8>(MaxLevel)) {
                ++level;
                bits >>= 1;
            }
            return level;
        }

        Node  m_head; // sentinel; level field unused, all MaxLevel pointers active
        usize m_size;
    };

} // namespace FoundationKitCxxStl::Structure
