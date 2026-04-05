#include <FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp>

namespace FoundationKitCxxStl {

    void RbTreeBase::Insert(RbNode* node, RbNode* parent, RbNode** link) noexcept {
        FK_BUG_ON(node == nullptr, "RbTreeBase::Insert: null node");
        FK_BUG_ON(link == nullptr, "RbTreeBase::Insert: null link pointer");
        node->SetParent(parent);
        node->left = node->right = nullptr;
        node->SetRed();
        *link = node;
        m_size++;
        InsertFixup(node);
    }

    void RbTreeBase::RotateLeft(RbNode* node) noexcept {
        RbNode* right = node->right;
        RbNode* parent = node->Parent();

        node->right = right->left;
        if (right->left) right->left->SetParent(node);

        right->left = node;
        right->SetParent(parent);

        if (parent) {
            if (parent->left == node) parent->left = right;
            else parent->right = right;
        } else {
            m_root = right;
        }
        node->SetParent(right);
    }

    void RbTreeBase::RotateRight(RbNode* node) noexcept {
        RbNode* left = node->left;
        RbNode* parent = node->Parent();

        node->left = left->right;
        if (left->right) left->right->SetParent(node);

        left->right = node;
        left->SetParent(parent);

        if (parent) {
            if (parent->right == node) parent->right = left;
            else parent->left = left;
        } else {
            m_root = left;
        }
        node->SetParent(left);
    }

    void RbTreeBase::InsertFixup(RbNode* node) noexcept {
        while (node->Parent() && node->Parent()->IsRed()) {
            RbNode* parent = node->Parent();
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
                        parent = node->Parent();
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
                        parent = node->Parent();
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

    void RbTreeBase::Remove(RbNode* node) noexcept {
        FK_BUG_ON(node == nullptr, "RbTreeBase::Remove: null node");
        FK_BUG_ON(m_size == 0, "RbTreeBase::Remove: size is zero (removing from empty tree)");
        RbNode* child;
        RbNode* parent;
        RbColor color;

        if (node->left && node->right) {
            RbNode* old = node;
            node = node->right;
            while (node->left) node = node->left;

            child = node->right;
            parent = node->Parent();
            color = node->Color();

            if (child) child->SetParent(parent);
            if (parent) {
                if (parent->left == node) parent->left = child;
                else parent->right = child;
            } else {
                m_root = child;
            }

            if (node->Parent() == old) parent = node;

            node->SetParent(old->Parent());
            node->SetColor(old->Color());
            node->left = old->left;
            node->right = old->right;

            if (old->Parent()) {
                if (old->Parent()->left == old) old->Parent()->left = node;
                else old->Parent()->right = node;
            } else {
                m_root = node;
            }

            old->left->SetParent(node);
            if (old->right) old->right->SetParent(node);
            
            goto fixup;
        }

        child = node->left ? node->left : node->right;
        parent = node->Parent();
        color = node->Color();

        if (child) child->SetParent(parent);
        if (parent) {
            if (parent->left == node) parent->left = child;
            else parent->right = child;
        } else {
            m_root = child;
        }

    fixup:
        if (color == RbColor::Black) {
            RemoveFixup(child, parent);
        }
        m_size--;
    }

    void RbTreeBase::RemoveFixup(RbNode* child, RbNode* parent) noexcept {
        while ((!child || child->IsBlack()) && child != m_root) {
            if (child == parent->left) {
                RbNode* sibling = parent->right;
                if (sibling->IsRed()) {
                    sibling->SetBlack();
                    parent->SetRed();
                    RotateLeft(parent);
                    sibling = parent->right;
                }
                if ((!sibling->left || sibling->left->IsBlack()) &&
                    (!sibling->right || sibling->right->IsBlack())) {
                    sibling->SetRed();
                    child = parent;
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
                if ((!sibling->left || sibling->left->IsBlack()) &&
                    (!sibling->right || sibling->right->IsBlack())) {
                    sibling->SetRed();
                    child = parent;
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

    RbNode* RbTreeBase::Next(RbNode* node) noexcept {
        FK_BUG_ON(node == nullptr, "RbTreeBase::Next: null node");
        if (node->right) {
            node = node->right;
            while (node->left) node = node->left;
            return node;
        }
        RbNode* parent = node->Parent();
        while (parent && node == parent->right) {
            node = parent;
            parent = parent->Parent();
        }
        return parent;
    }

    RbNode* RbTreeBase::Prev(RbNode* node) noexcept {
        FK_BUG_ON(node == nullptr, "RbTreeBase::Prev: null node");
        if (node->left) {
            node = node->left;
            while (node->right) node = node->right;
            return node;
        }
        RbNode* parent = node->Parent();
        while (parent && node == parent->left) {
            node = parent;
            parent = parent->Parent();
        }
        return parent;
    }

    RbNode* RbTreeBase::First() const noexcept {
        RbNode* node = m_root;
        if (!node) return nullptr;
        while (node->left) node = node->left;
        return node;
    }

    RbNode* RbTreeBase::Last() const noexcept {
        RbNode* node = m_root;
        if (!node) return nullptr;
        while (node->right) node = node->right;
        return node;
    }

} // namespace FoundationKitCxxStl
