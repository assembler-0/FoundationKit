#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Structure {

    /// @brief A zero-allocation singly linked list that uses intrusive nodes.
    /// @tparam T The type that contains the Node.
    template <typename T>
    class IntrusiveSinglyLinkedList {
    public:
        struct Node {
            Node* next = nullptr;
        };

        IntrusiveSinglyLinkedList() : m_head(nullptr), m_size(0) {}

        void PushFront(Node* node) {
            FK_BUG_ON(node == nullptr, "IntrusiveSinglyLinkedList::PushFront: null node");
            // A node whose next pointer is non-null is already in a list.
            // Inserting it again would corrupt both lists.
            FK_BUG_ON(node->next != nullptr,
                "IntrusiveSinglyLinkedList::PushFront: node->next is non-null (node already linked or not zeroed after PopFront)");
            node->next = m_head;
            m_head = node;
            m_size++;
        }

        Node* PopFront() {
            FK_BUG_ON(!m_head, "IntrusiveSinglyLinkedList: PopFront called on empty list");
            if (!m_head) return nullptr;
            Node* node = m_head;
            m_head = m_head->next;
            // Zero the popped node's next pointer so a subsequent PushFront
            // of the same node doesn't falsely trigger the already-linked check.
            node->next = nullptr;
            m_size--;
            return node;
        }

        [[nodiscard]] Node* Front() { 
            FK_BUG_ON(!m_head, "IntrusiveSinglyLinkedList: Front called on empty list");
            return m_head; 
        }
        [[nodiscard]] const Node* Front() const { 
            FK_BUG_ON(!m_head, "IntrusiveSinglyLinkedList: Front called on empty list");
            return m_head; 
        }

        [[nodiscard]] bool Empty() const { return m_head == nullptr; }
        [[nodiscard]] usize Size() const { return m_size; }

        void Clear() {
            m_head = nullptr;
            m_size = 0;
        }

        void Reset() noexcept {
            Clear();
        }

    private:
        Node* m_head;
        usize m_size;
    };

} // namespace FoundationKitCxxStl::Structure
