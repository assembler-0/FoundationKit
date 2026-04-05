#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl {

    enum class RbColor : u8 {
        Red,
        Black
    };

    struct RbNode {
        uptr parent_color;
        RbNode* left;
        RbNode* right;

        static constexpr uptr ColorMask = 1UL;
        static constexpr uptr ParentMask = ~ColorMask;

        constexpr RbNode() noexcept
            : parent_color(0), left(nullptr), right(nullptr) {}

        [[nodiscard]] RbNode* Parent() const noexcept {
            return reinterpret_cast<RbNode*>(parent_color & ParentMask);
        }

        void SetParent(RbNode* parent) noexcept {
            parent_color = (reinterpret_cast<uptr>(parent) & ParentMask) | (parent_color & ColorMask);
        }

        [[nodiscard]] RbColor Color() const noexcept {
            return static_cast<RbColor>(parent_color & ColorMask);
        }

        void SetColor(RbColor color) noexcept {
            parent_color = (parent_color & ParentMask) | static_cast<uptr>(color);
        }

        [[nodiscard]] bool IsRed() const noexcept { return Color() == RbColor::Red; }
        [[nodiscard]] bool IsBlack() const noexcept { return Color() == RbColor::Black; }
        
        void SetRed() noexcept { SetColor(RbColor::Red); }
        void SetBlack() noexcept { SetColor(RbColor::Black); }
    };

    /// @brief Base Red-Black Tree implementation (Intrusive).
    /// Does not handle dynamic memory; works with RbNode pointers.
    class RbTreeBase {
    public:
        constexpr RbTreeBase() noexcept : m_root(nullptr), m_size(0) {}

        [[nodiscard]] constexpr bool Empty() const noexcept { return m_root == nullptr; }
        [[nodiscard]] constexpr usize Size() const noexcept { return m_size; }

        void Insert(RbNode* node, RbNode* parent, RbNode** link) noexcept;
        void Remove(RbNode* node) noexcept;

        static RbNode* Next(RbNode* node) noexcept;
        static RbNode* Prev(RbNode* node) noexcept;
        
        RbNode* First() const noexcept;
        RbNode* Last() const noexcept;

    protected:
        RbNode* m_root;
        usize m_size;

    private:
        void RotateLeft(RbNode* node) noexcept;
        void RotateRight(RbNode* node) noexcept;
        void InsertFixup(RbNode* node) noexcept;
        void RemoveFixup(RbNode* child, RbNode* parent) noexcept;
    };

    /// @brief Typed Intrusive Red-Black Tree.
    /// T must have a member accessible via NodeOffset, or inherit from RbNode.
    template <typename T, usize NodeOffset>
    class IntrusiveRbTree : public RbTreeBase {
        static_assert(NodeOffset < 65536,
            "IntrusiveRbTree: NodeOffset exceeds 65535 bytes — did you swap T and NodeOffset?");
    public:
        static T* ToEntry(RbNode* node) noexcept {
            if (!node) return nullptr;
            return reinterpret_cast<T*>(reinterpret_cast<uptr>(node) - NodeOffset);
        }

        static const T* ToEntry(const RbNode* node) noexcept {
            if (!node) return nullptr;
            return reinterpret_cast<const T*>(reinterpret_cast<uptr>(node) - NodeOffset);
        }

        static RbNode* ToNode(T* entry) noexcept {
            if (!entry) return nullptr;
            return reinterpret_cast<RbNode*>(reinterpret_cast<uptr>(entry) + NodeOffset);
        }

        template <typename Key, typename Comparator>
        T* Find(const Key& key, Comparator&& comp) const noexcept {
            RbNode* node = m_root;
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
            FK_BUG_ON(entry == nullptr, "IntrusiveRbTree::Insert: null entry");
            RbNode* node = ToNode(entry);
            FK_BUG_ON(node->Parent() != nullptr || node->left != nullptr || node->right != nullptr,
                "IntrusiveRbTree::Insert: node appears already linked (parent/children non-null)");
            RbNode** link = &m_root;
            RbNode* parent = nullptr;

            while (*link) {
                parent = *link;
                int res = comp(*entry, *ToEntry(parent));
                if (res < 0) link = &parent->left;
                else link = &parent->right;
            }

            RbTreeBase::Insert(node, parent, link);
        }

        void Remove(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr, "IntrusiveRbTree::Remove: null entry");
            RbTreeBase::Remove(ToNode(entry));
        }

        T* First() const noexcept { return ToEntry(RbTreeBase::First()); }
        T* Last() const noexcept { return ToEntry(RbTreeBase::Last()); }
        T* Next(T* entry) const noexcept { return ToEntry(RbTreeBase::Next(ToNode(entry))); }
        T* Prev(T* entry) const noexcept { return ToEntry(RbTreeBase::Prev(ToNode(entry))); }
    };

} // namespace FoundationKitCxxStl
