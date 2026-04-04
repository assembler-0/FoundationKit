#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    /// @brief A circular singly linked list.
    /// @tparam T The type of elements.
    /// @tparam Alloc The allocator to use for nodes.
    template <typename T, FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class CircularLinkedList {
    public:
        struct Node {
            T value;
            Node* next;

            template <typename... Args>
            explicit Node(Node* n, Args&&... args)
                : value(FoundationKitCxxStl::Forward<Args>(args)...), next(n) {}
        };

        class Iterator {
        public:
            Iterator(Node* node, const bool is_end) : m_node(node), m_is_end(is_end) {}

            T& operator*() const { return m_node->value; }
            T* operator->() const { return &m_node->value; }

            Iterator& operator++() {
                m_node = m_node->next;
                return *this;
            }

            Iterator operator++(int) {
                Iterator tmp = *this;
                m_node = m_node->next;
                return tmp;
            }

            bool operator==(const Iterator& other) const { 
                return m_node == other.m_node && m_is_end == other.m_is_end; 
            }
            bool operator!=(const Iterator& other) const { return !(*this == other); }

            Node* GetNode() const { return m_node; }

        private:
            Node* m_node;
            bool  m_is_end;
        };

        explicit CircularLinkedList(Alloc allocator = Alloc())
            : m_allocator(FoundationKitCxxStl::Move(allocator)), m_last(nullptr), m_size(0) {}

        ~CircularLinkedList() { Clear(); }

        CircularLinkedList(const CircularLinkedList&) = delete;
        CircularLinkedList& operator=(const CircularLinkedList&) = delete;

        CircularLinkedList(CircularLinkedList&& other) noexcept
            : m_allocator(FoundationKitCxxStl::Move(other.m_allocator)), m_last(other.m_last), m_size(other.m_size) {
            other.m_last = nullptr;
            other.m_size = 0;
        }

        CircularLinkedList& operator=(CircularLinkedList&& other) noexcept {
            if (this != &other) {
                Clear();
                m_allocator = FoundationKitCxxStl::Move(other.m_allocator);
                m_last = other.m_last;
                m_size = other.m_size;
                other.m_last = nullptr;
                other.m_size = 0;
            }
            return *this;
        }

        template <typename... Args>
        bool PushFront(Args&&... args) {
            auto res = m_allocator.Allocate(sizeof(Node), alignof(Node));
            if (!res.ok()) return false;

            if (!m_last) {
                m_last = FoundationKitCxxStl::ConstructAt<Node>(res.ptr, nullptr, FoundationKitCxxStl::Forward<Args>(args)...);
                m_last->next = m_last;
            } else {
                Node* new_node = FoundationKitCxxStl::ConstructAt<Node>(res.ptr, m_last->next, FoundationKitCxxStl::Forward<Args>(args)...);
                m_last->next = new_node;
            }
            m_size++;
            return true;
        }

        template <typename... Args>
        bool PushBack(Args&&... args) {
            const bool ok = PushFront(FoundationKitCxxStl::Forward<Args>(args)...);
            if (ok) {
                m_last = m_last->next;
            }
            return ok;
        }

        void PopFront() {
            FK_BUG_ON(!m_last, "CircularLinkedList: PopFront called on empty list");
            if (!m_last) return;

            Node* head = m_last->next;
            if (head == m_last) {
                m_last = nullptr;
            } else {
                m_last->next = head->next;
            }

            head->~Node();
            m_allocator.Deallocate(head, sizeof(Node));
            m_size--;
        }

        void Clear() {
            while (m_last) {
                PopFront();
            }
        }

        void Rotate() {
            if (m_last) {
                m_last = m_last->next;
            }
        }

        [[nodiscard]] usize Size() const { return m_size; }
        [[nodiscard]] bool Empty() const { return m_size == 0; }

        [[nodiscard]] T& Front() { 
            FK_BUG_ON(!m_last, "CircularLinkedList: Front called on empty list");
            return m_last->next->value; 
        }
        [[nodiscard]] const T& Front() const { 
            FK_BUG_ON(!m_last, "CircularLinkedList: Front called on empty list");
            return m_last->next->value; 
        }
        [[nodiscard]] T& Back() { 
            FK_BUG_ON(!m_last, "CircularLinkedList: Back called on empty list");
            return m_last->value; 
        }
        [[nodiscard]] const T& Back() const { 
            FK_BUG_ON(!m_last, "CircularLinkedList: Back called on empty list");
            return m_last->value; 
        }

        // Circular iterators are tricky. Usually you stop after one full loop.
        // For standard begin/end we return a special end iterator or handle it via a counter.
        // Here, we provide a basic begin/end that only works for a single pass if head is known.
        Iterator begin() { return Iterator(m_last ? m_last->next : nullptr, false); }
        Iterator end() { return Iterator(m_last ? m_last->next : nullptr, true); }

    private:
        Alloc m_allocator;
        Node* m_last;
        usize m_size;
    };

} // namespace FoundationKitCxxStl::Structure
