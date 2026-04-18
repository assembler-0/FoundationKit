#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Algorithm.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl {

    struct AvlNode {
        AvlNode* parent;
        AvlNode* left;
        AvlNode* right;
        isize height;

        constexpr AvlNode() noexcept
            : parent(nullptr), left(nullptr), right(nullptr), height(1) {}
    };

    class AvlTreeBase {
    public:
        constexpr AvlTreeBase() noexcept : m_root(nullptr), m_size(0) {}

        [[nodiscard]] constexpr bool Empty() const noexcept { return m_root == nullptr; }
        [[nodiscard]] constexpr usize Size() const noexcept { return m_size; }

        void Insert(AvlNode* node, AvlNode* parent, AvlNode** link) noexcept;
        void Remove(AvlNode* node) noexcept;

        static AvlNode* Next(AvlNode* node) noexcept;
        static AvlNode* Prev(AvlNode* node) noexcept;

        AvlNode* First() const noexcept;
        AvlNode* Last() const noexcept;

    protected:
        AvlNode* m_root;
        usize m_size;

    private:
        static isize GetHeight(AvlNode* node) noexcept { return node ? node->height : 0; }
        static void UpdateHeight(AvlNode* node) noexcept {
            node->height = 1 + Max(GetHeight(node->left), GetHeight(node->right));
        }
        static isize GetBalance(AvlNode* node) noexcept {
            return node ? GetHeight(node->left) - GetHeight(node->right) : 0;
        }

        void RotateLeft(AvlNode* node) noexcept;
        void RotateRight(AvlNode* node) noexcept;
        void Rebalance(AvlNode* node) noexcept;
    };

    template <typename T, usize NodeOffset>
    class IntrusiveAvlTree : public AvlTreeBase {
        // NodeOffset must be within a plausible struct size — catches transposed arguments.
        static_assert(NodeOffset < 65536,
            "IntrusiveAvlTree: NodeOffset exceeds 65535 bytes — did you swap T and NodeOffset?");
    public:
        static T* ToEntry(AvlNode* node) noexcept {
            if (!node) return nullptr;
            return reinterpret_cast<T*>(reinterpret_cast<uptr>(node) - NodeOffset);
        }

        static AvlNode* ToNode(T* entry) noexcept {
            if (!entry) return nullptr;
            return reinterpret_cast<AvlNode*>(reinterpret_cast<uptr>(entry) + NodeOffset);
        }

        template <typename Key, typename Comparator>
        T* Find(const Key& key, Comparator&& comp) const noexcept {
            AvlNode* node = m_root;
            while (node) {
                T* entry = ToEntry(node);
                int res = comp(key, *entry);
                if (res == 0) return entry;
                node = (res < 0) ? node->left : node->right;
            }
            return nullptr;
        }

        template <typename Comparator>
        void Insert(T* entry, Comparator&& comp) noexcept {
            FK_BUG_ON(entry == nullptr, "IntrusiveAvlTree::Insert: null entry");
            AvlNode* node = ToNode(entry);
            // A node already linked into a tree has a non-null parent or is the root.
            FK_BUG_ON(node->parent != nullptr || node->left != nullptr || node->right != nullptr,
                "IntrusiveAvlTree::Insert: node appears already linked (parent/children non-null)");
            AvlNode** link = &m_root;
            AvlNode* parent = nullptr;

            while (*link) {
                parent = *link;
                int res = comp(*entry, *ToEntry(parent));
                if (res < 0) link = &parent->left;
                else link = &parent->right;
            }

            AvlTreeBase::Insert(node, parent, link);
        }

        void Remove(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr, "IntrusiveAvlTree::Remove: null entry");
            AvlTreeBase::Remove(ToNode(entry));
        }

        T* First() const noexcept { return ToEntry(AvlTreeBase::First()); }
        T* Last() const noexcept { return ToEntry(AvlTreeBase::Last()); }
        T* Next(T* entry) const noexcept { return ToEntry(AvlTreeBase::Next(ToNode(entry))); }
        T* Prev(T* entry) const noexcept { return ToEntry(AvlTreeBase::Prev(ToNode(entry))); }
    };

} // namespace FoundationKitCxxStl
