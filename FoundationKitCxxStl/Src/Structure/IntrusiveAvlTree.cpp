#include <FoundationKitCxxStl/Structure/IntrusiveAvlTree.hpp>

namespace FoundationKitCxxStl {

    void AvlTreeBase::RotateLeft(AvlNode* node) noexcept {
        AvlNode* right = node->right;
        AvlNode* parent = node->parent;

        node->right = right->left;
        if (right->left) right->left->parent = node;

        right->left = node;
        right->parent = parent;

        if (parent) {
            if (parent->left == node) parent->left = right;
            else parent->right = right;
        } else {
            m_root = right;
        }
        node->parent = right;

        UpdateHeight(node);
        UpdateHeight(right);
    }

    void AvlTreeBase::RotateRight(AvlNode* node) noexcept {
        AvlNode* left = node->left;
        AvlNode* parent = node->parent;

        node->left = left->right;
        if (left->right) left->right->parent = node;

        left->right = node;
        left->parent = parent;

        if (parent) {
            if (parent->right == node) parent->right = left;
            else parent->left = left;
        } else {
            m_root = left;
        }
        node->parent = left;

        UpdateHeight(node);
        UpdateHeight(left);
    }

    void AvlTreeBase::Rebalance(AvlNode* node) noexcept {
        while (node) {
            UpdateHeight(node);
            isize balance = GetBalance(node);

            if (balance > 1) {
                if (GetBalance(node->left) < 0) {
                    RotateLeft(node->left);
                }
                RotateRight(node);
            } else if (balance < -1) {
                if (GetBalance(node->right) > 0) {
                    RotateRight(node->right);
                }
                RotateLeft(node);
            }
            node = node->parent;
        }
    }

    void AvlTreeBase::Insert(AvlNode* node, AvlNode* parent, AvlNode** link) noexcept {
        node->parent = parent;
        node->left = node->right = nullptr;
        node->height = 1;
        *link = node;
        m_size++;
        Rebalance(parent);
    }

    void AvlTreeBase::Remove(AvlNode* node) noexcept {
        AvlNode* child;
        AvlNode* parent;

        if (node->left && node->right) {
            AvlNode* old = node;
            node = node->right;
            while (node->left) node = node->left;

            child = node->right;
            parent = node->parent;

            if (child) child->parent = parent;
            if (parent) {
                if (parent->left == node) parent->left = child;
                else parent->right = child;
            } else {
                m_root = child;
            }

            if (node->parent == old) parent = node;

            node->parent = old->parent;
            node->left = old->left;
            node->right = old->right;

            if (old->parent) {
                if (old->parent->left == old) old->parent->left = node;
                else old->parent->right = node;
            } else {
                m_root = node;
            }

            old->left->parent = node;
            if (old->right) old->right->parent = node;
            
            goto fixup;
        }

        child = node->left ? node->left : node->right;
        parent = node->parent;

        if (child) child->parent = parent;
        if (parent) {
            if (parent->left == node) parent->left = child;
            else parent->right = child;
        } else {
            m_root = child;
        }

    fixup:
        m_size--;
        Rebalance(parent);
    }

    AvlNode* AvlTreeBase::Next(AvlNode* node) noexcept {
        if (node->right) {
            node = node->right;
            while (node->left) node = node->left;
            return node;
        }
        AvlNode* parent = node->parent;
        while (parent && node == parent->right) {
            node = parent;
            parent = parent->parent;
        }
        return parent;
    }

    AvlNode* AvlTreeBase::Prev(AvlNode* node) noexcept {
        if (node->left) {
            node = node->left;
            while (node->right) node = node->right;
            return node;
        }
        AvlNode* parent = node->parent;
        while (parent && node == parent->left) {
            node = parent;
            parent = parent->parent;
        }
        return parent;
    }

    AvlNode* AvlTreeBase::First() const noexcept {
        AvlNode* node = m_root;
        if (!node) return nullptr;
        while (node->left) node = node->left;
        return node;
    }

    AvlNode* AvlTreeBase::Last() const noexcept {
        AvlNode* node = m_root;
        if (!node) return nullptr;
        while (node->right) node = node->right;
        return node;
    }

} // namespace FoundationKitCxxStl
