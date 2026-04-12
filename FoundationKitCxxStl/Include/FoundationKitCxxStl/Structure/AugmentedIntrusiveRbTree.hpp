#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp>

namespace FoundationKitCxxStl {

    // =========================================================================
    // AugmentedIntrusiveRbTree<T, NodeOffset, Derived>
    //
    // ## What augmentation means
    //
    // An augmented RB-tree stores an extra value in each node that is a
    // function of the node's subtree (e.g., the maximum endpoint in an
    // interval tree, the subtree sum in an order-statistics tree). The
    // invariant must be maintained after every structural change: insert,
    // remove, and — critically — every rotation.
    //
    // ## Why CRTP and not a function pointer
    //
    // A function pointer per-tree costs 8 bytes and one indirect call per
    // rotation. In a hot path (VMA lookup during page fault), that indirect
    // call defeats branch prediction. CRTP resolves the call at compile time
    // with zero overhead: the compiler inlines Derived::Propagate directly
    // into the rotation body.
    //
    // ## Propagation contract
    //
    // Derived must implement:
    //
    //   static void Propagate(RbNode* node) noexcept;
    //
    // Called bottom-up after every rotation and after insert/remove fixup.
    // `node` is never null when Propagate is called. The implementation must
    // recompute node's augmented value from node->left and node->right.
    //
    // ## Relationship to RbTreeBase
    //
    // AugmentedRbTreeBase is a *separate* implementation, not a subclass of
    // RbTreeBase. Inheriting from RbTreeBase and overriding rotations would
    // require virtual dispatch (banned). Duplicating the ~120 lines of fixup
    // logic with the Propagate hook injected is the correct trade-off: the
    // augmented and non-augmented paths are fully independent, and the
    // compiler can inline everything.
    //
    // =========================================================================

    /// @brief CRTP base for an augmented intrusive RB-tree.
    ///
    /// @tparam Derived  The concrete tree class. Must implement:
    ///                  `static void Propagate(RbNode*) noexcept`
    template <typename Derived>
    class AugmentedRbTreeBase {
    public:
        constexpr AugmentedRbTreeBase() noexcept : m_root(nullptr), m_size(0) {}

        [[nodiscard]] constexpr bool  Empty() const noexcept { return m_root == nullptr; }
        [[nodiscard]] constexpr usize Size()  const noexcept { return m_size; }
        [[nodiscard]] RbNode* Root() const noexcept { return m_root; }

        void Insert(RbNode* node, RbNode* parent, RbNode** link) noexcept;
        void Remove(RbNode* node) noexcept;

        static RbNode* Next(RbNode* node) noexcept;
        static RbNode* Prev(RbNode* node) noexcept;
        [[nodiscard]] RbNode* First() const noexcept;
        [[nodiscard]] RbNode* Last()  const noexcept;

    protected:
        RbNode* m_root;
        usize   m_size;

    private:
        // Propagate is called on `node` after its subtree changes.
        // The cast is safe: Derived inherits from AugmentedRbTreeBase<Derived>.
        void CallPropagate(RbNode* node) noexcept {
            if (node) Derived::Propagate(node);
        }

        void RotateLeft(RbNode* node) noexcept;
        void RotateRight(RbNode* node) noexcept;
        void InsertFixup(RbNode* node) noexcept;
        void RemoveFixup(RbNode* child, RbNode* parent) noexcept;
    };

    // -------------------------------------------------------------------------
    // AugmentedRbTreeBase — out-of-line method bodies (header-only, templates)
    // -------------------------------------------------------------------------

    template <typename Derived>
    void AugmentedRbTreeBase<Derived>::RotateLeft(RbNode* node) noexcept {
        RbNode* right  = node->right;
        RbNode* parent = node->Parent();

        node->right = right->left;
        if (right->left) right->left->SetParent(node);

        right->left = node;
        right->SetParent(parent);

        if (parent) {
            if (parent->left == node) parent->left  = right;
            else                      parent->right = right;
        } else {
            m_root = right;
        }
        node->SetParent(right);

        // Propagate bottom-up: node's subtree changed first, then right's.
        CallPropagate(node);
        CallPropagate(right);
    }

    template <typename Derived>
    void AugmentedRbTreeBase<Derived>::RotateRight(RbNode* node) noexcept {
        RbNode* left   = node->left;
        RbNode* parent = node->Parent();

        node->left = left->right;
        if (left->right) left->right->SetParent(node);

        left->right = node;
        left->SetParent(parent);

        if (parent) {
            if (parent->right == node) parent->right = left;
            else                       parent->left  = left;
        } else {
            m_root = left;
        }
        node->SetParent(left);

        CallPropagate(node);
        CallPropagate(left);
    }

    template <typename Derived>
    void AugmentedRbTreeBase<Derived>::Insert(RbNode* node, RbNode* parent, RbNode** link) noexcept {
        FK_BUG_ON(node == nullptr, "AugmentedRbTreeBase::Insert: null node");
        FK_BUG_ON(link == nullptr, "AugmentedRbTreeBase::Insert: null link pointer");
        node->SetParent(parent);
        node->left = node->right = nullptr;
        node->SetRed();
        *link = node;
        m_size++;
        InsertFixup(node);
        RbNode* p = node;
        while (p) { CallPropagate(p); p = p->Parent(); }
    }

    template <typename Derived>
    void AugmentedRbTreeBase<Derived>::InsertFixup(RbNode* node) noexcept {
        while (node->Parent() && node->Parent()->IsRed()) {
            RbNode* parent      = node->Parent();
            RbNode* grandparent = parent->Parent();

            if (parent == grandparent->left) {
                RbNode* uncle = grandparent->right;
                if (uncle && uncle->IsRed()) {
                    parent->SetBlack();
                    uncle->SetBlack();
                    grandparent->SetRed();
                    node = grandparent;
                } else {
                    if (node == parent->right) {
                        node = parent;
                        RotateLeft(node);
                        parent      = node->Parent();
                        grandparent = parent->Parent();
                    }
                    parent->SetBlack();
                    grandparent->SetRed();
                    RotateRight(grandparent);
                }
            } else {
                RbNode* uncle = grandparent->left;
                if (uncle && uncle->IsRed()) {
                    parent->SetBlack();
                    uncle->SetBlack();
                    grandparent->SetRed();
                    node = grandparent;
                } else {
                    if (node == parent->left) {
                        node = parent;
                        RotateRight(node);
                        parent      = node->Parent();
                        grandparent = parent->Parent();
                    }
                    parent->SetBlack();
                    grandparent->SetRed();
                    RotateLeft(grandparent);
                }
            }
        }
        m_root->SetBlack();
    }

    template <typename Derived>
    void AugmentedRbTreeBase<Derived>::Remove(RbNode* node) noexcept {
        FK_BUG_ON(node == nullptr, "AugmentedRbTreeBase::Remove: null node");
        FK_BUG_ON(m_size == 0,    "AugmentedRbTreeBase::Remove: size is zero (removing from empty tree)");

        RbNode* child;
        RbNode* parent;
        RbColor color;
        // deepest is the lowest node whose subtree changed; we propagate
        // bottom-up from there after RemoveFixup completes.
        RbNode* deepest = nullptr;

        if (node->left && node->right) {
            RbNode* old = node;
            node = node->right;
            while (node->left) node = node->left;

            child  = node->right;
            parent = node->Parent();
            color  = node->Color();

            if (child) child->SetParent(parent);
            if (parent) {
                if (parent->left == node) parent->left  = child;
                else                      parent->right = child;
            } else {
                m_root = child;
            }

            if (node->Parent() == old) parent = node;

            node->SetParent(old->Parent());
            node->SetColor(old->Color());
            node->left  = old->left;
            node->right = old->right;

            if (old->Parent()) {
                if (old->Parent()->left == old) old->Parent()->left  = node;
                else                            old->Parent()->right = node;
            } else {
                m_root = node;
            }

            old->left->SetParent(node);
            if (old->right) old->right->SetParent(node);

            // The lowest structurally changed node is `parent` (the splice
            // point), or `node` itself if the successor was a direct child.
            deepest = parent;
            goto fixup;
        }

        child  = node->left ? node->left : node->right;
        parent = node->Parent();
        color  = node->Color();

        if (child) child->SetParent(parent);
        if (parent) {
            if (parent->left == node) parent->left  = child;
            else                      parent->right = child;
        } else {
            m_root = child;
        }
        deepest = parent;

    fixup:
        if (color == RbColor::Black) RemoveFixup(child, parent);
        m_size--;

        RbNode* p = deepest;
        while (p) { CallPropagate(p); p = p->Parent(); }
    }

    template <typename Derived>
    void AugmentedRbTreeBase<Derived>::RemoveFixup(RbNode* child, RbNode* parent) noexcept {
        // Identical double-black fixup to RbTreeBase::RemoveFixup.
        // Rotations inside call CallPropagate automatically.
        while ((!child || child->IsBlack()) && child != m_root) {
            if (child == parent->left) {
                RbNode* sibling = parent->right;
                if (sibling->IsRed()) {
                    sibling->SetBlack();
                    parent->SetRed();
                    RotateLeft(parent);
                    sibling = parent->right;
                }
                if ((!sibling->left  || sibling->left->IsBlack()) &&
                    (!sibling->right || sibling->right->IsBlack())) {
                    sibling->SetRed();
                    child  = parent;
                    parent = child->Parent();
                } else {
                    if (!sibling->right || sibling->right->IsBlack()) {
                        if (sibling->left) sibling->left->SetBlack();
                        sibling->SetRed();
                        RotateRight(sibling);
                        sibling = parent->right;
                    }
                    sibling->SetColor(parent->Color());
                    parent->SetBlack();
                    if (sibling->right) sibling->right->SetBlack();
                    RotateLeft(parent);
                    child = m_root;
                }
            } else {
                RbNode* sibling = parent->left;
                if (sibling->IsRed()) {
                    sibling->SetBlack();
                    parent->SetRed();
                    RotateRight(parent);
                    sibling = parent->left;
                }
                if ((!sibling->left  || sibling->left->IsBlack()) &&
                    (!sibling->right || sibling->right->IsBlack())) {
                    sibling->SetRed();
                    child  = parent;
                    parent = child->Parent();
                } else {
                    if (!sibling->left || sibling->left->IsBlack()) {
                        if (sibling->right) sibling->right->SetBlack();
                        sibling->SetRed();
                        RotateLeft(sibling);
                        sibling = parent->left;
                    }
                    sibling->SetColor(parent->Color());
                    parent->SetBlack();
                    if (sibling->left) sibling->left->SetBlack();
                    RotateRight(parent);
                    child = m_root;
                }
            }
        }
        if (child) child->SetBlack();
    }

    template <typename Derived>
    RbNode* AugmentedRbTreeBase<Derived>::Next(RbNode* node) noexcept {
        FK_BUG_ON(node == nullptr, "AugmentedRbTreeBase::Next: null node");
        if (node->right) {
            node = node->right;
            while (node->left) node = node->left;
            return node;
        }
        RbNode* parent = node->Parent();
        while (parent && node == parent->right) { node = parent; parent = parent->Parent(); }
        return parent;
    }

    template <typename Derived>
    RbNode* AugmentedRbTreeBase<Derived>::Prev(RbNode* node) noexcept {
        FK_BUG_ON(node == nullptr, "AugmentedRbTreeBase::Prev: null node");
        if (node->left) {
            node = node->left;
            while (node->right) node = node->right;
            return node;
        }
        RbNode* parent = node->Parent();
        while (parent && node == parent->left) { node = parent; parent = parent->Parent(); }
        return parent;
    }

    template <typename Derived>
    RbNode* AugmentedRbTreeBase<Derived>::First() const noexcept {
        RbNode* node = m_root;
        if (!node) return nullptr;
        while (node->left) node = node->left;
        return node;
    }

    template <typename Derived>
    RbNode* AugmentedRbTreeBase<Derived>::Last() const noexcept {
        RbNode* node = m_root;
        if (!node) return nullptr;
        while (node->right) node = node->right;
        return node;
    }

    // =========================================================================
    // AugmentedIntrusiveRbTree<T, NodeOffset, Derived>
    //
    // Typed wrapper. Derived must inherit from this class and implement:
    //   static void Propagate(RbNode* node) noexcept;
    //
    // ToEntry() is exposed as a static so Propagate() can use it without
    // needing a tree instance.
    // =========================================================================

    /// @brief Typed augmented intrusive RB-tree.
    ///
    /// @tparam T          Container type embedding an RbNode member.
    /// @tparam NodeOffset Byte offset of the RbNode within T (use FOUNDATIONKITCXXSTL_OFFSET_OF).
    /// @tparam Derived    The concrete subclass implementing Propagate().
    template <typename T, usize NodeOffset, typename Derived>
    class AugmentedIntrusiveRbTree : public AugmentedRbTreeBase<Derived> {
        static_assert(NodeOffset < 65536,
            "AugmentedIntrusiveRbTree: NodeOffset exceeds 65535 bytes — did you swap T and NodeOffset?");

        using Base = AugmentedRbTreeBase<Derived>;

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

        /// @brief Find an entry by key.
        /// @param comp  comp(key, entry) returns negative/zero/positive.
        template <typename Key, typename Comparator>
        T* Find(const Key& key, Comparator&& comp) const noexcept {
            RbNode* node = Base::m_root;
            while (node) {
                T* entry = ToEntry(node);
                int res  = comp(key, *entry);
                if (res == 0) return entry;
                node = (res < 0) ? node->left : node->right;
            }
            return nullptr;
        }

        /// @brief Insert an entry. Calls Derived::Propagate bottom-up.
        /// @param comp  comp(a, b) returns negative/zero/positive.
        template <typename Comparator>
        void Insert(T* entry, Comparator&& comp) noexcept {
            FK_BUG_ON(entry == nullptr, "AugmentedIntrusiveRbTree::Insert: null entry");
            RbNode* node = ToNode(entry);
            FK_BUG_ON(node->Parent() != nullptr || node->left != nullptr || node->right != nullptr,
                "AugmentedIntrusiveRbTree::Insert: node appears already linked");

            RbNode** link  = &this->m_root;
            RbNode*  parent = nullptr;
            while (*link) {
                parent = *link;
                int res = comp(*entry, *ToEntry(parent));
                if (res < 0) link = &parent->left;
                else         link = &parent->right;
            }
            Base::Insert(node, parent, link);
        }

        /// @brief Remove an entry. Calls Derived::Propagate bottom-up.
        void Remove(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr, "AugmentedIntrusiveRbTree::Remove: null entry");
            Base::Remove(ToNode(entry));
        }

        T*    First() const noexcept { return ToEntry(Base::First()); }
        T*    Last()  const noexcept { return ToEntry(Base::Last()); }
        T*    Next(T* entry) const noexcept { return ToEntry(Base::Next(ToNode(entry))); }
        T*    Prev(T* entry) const noexcept { return ToEntry(Base::Prev(ToNode(entry))); }
    };

} // namespace FoundationKitCxxStl
