#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

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
            node->next = m_head;
            m_head = node;
            m_size++;
        }

        Node* PopFront() {
            if (!m_head) return nullptr;
            Node* node = m_head;
            m_head = m_head->next;
            m_size--;
            return node;
        }

        [[nodiscard]] Node* Front() { return m_head; }
        [[nodiscard]] const Node* Front() const { return m_head; }

        [[nodiscard]] bool Empty() const { return m_head == nullptr; }
        [[nodiscard]] usize Size() const { return m_size; }

        void Clear() {
            m_head = nullptr;
            m_size = 0;
        }

    private:
        Node* m_head;
        usize m_size;
    };

} // namespace FoundationKitCxxStl::Structure
