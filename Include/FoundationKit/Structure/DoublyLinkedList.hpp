#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Memory/AnyAllocator.hpp>

namespace FoundationKit::Structure {

    /// @brief A bidirectional doubly linked list.
    /// @tparam T The type of elements.
    /// @tparam Alloc The allocator to use for nodes.
    template <typename T, Memory::IAllocator Alloc = Memory::AnyAllocator>
    class DoublyLinkedList {
    public:
        struct Node {
            T value;
            Node* next;
            Node* prev;

            template <typename... Args>
            Node(Node* p, Node* n, Args&&... args)
                : value(FoundationKit::Forward<Args>(args)...), next(n), prev(p) {}
        };

        class Iterator {
        public:
            explicit Iterator(Node* node) : m_node(node) {}

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

            Iterator& operator--() {
                m_node = m_node->prev;
                return *this;
            }

            Iterator operator--(int) {
                Iterator tmp = *this;
                m_node = m_node->prev;
                return tmp;
            }

            bool operator==(const Iterator& other) const { return m_node == other.m_node; }
            bool operator!=(const Iterator& other) const { return m_node != other.m_node; }

            Node* GetNode() const { return m_node; }

        private:
            Node* m_node;
        };

        explicit DoublyLinkedList(Alloc allocator = Alloc())
            : m_allocator(FoundationKit::Move(allocator)), m_head(nullptr), m_tail(nullptr), m_size(0) {}

        ~DoublyLinkedList() { Clear(); }

        DoublyLinkedList(const DoublyLinkedList&) = delete;
        DoublyLinkedList& operator=(const DoublyLinkedList&) = delete;

        DoublyLinkedList(DoublyLinkedList&& other) noexcept
            : m_allocator(FoundationKit::Move(other.m_allocator)), m_head(other.m_head), m_tail(other.m_tail), m_size(other.m_size) {
            other.m_head = nullptr;
            other.m_tail = nullptr;
            other.m_size = 0;
        }

        DoublyLinkedList& operator=(DoublyLinkedList&& other) noexcept {
            if (this != &other) {
                Clear();
                m_allocator = FoundationKit::Move(other.m_allocator);
                m_head = other.m_head;
                m_tail = other.m_tail;
                m_size = other.m_size;
                other.m_head = nullptr;
                other.m_tail = nullptr;
                other.m_size = 0;
            }
            return *this;
        }

        template <typename... Args>
        bool PushFront(Args&&... args) {
            auto res = m_allocator.Allocate(sizeof(Node), alignof(Node));
            if (!res.ok()) return false;

            auto* new_node = FoundationKit::ConstructAt<Node>(res.ptr, nullptr, m_head, FoundationKit::Forward<Args>(args)...);
            if (m_head) {
                m_head->prev = new_node;
            } else {
                m_tail = new_node;
            }
            m_head = new_node;
            m_size++;
            return true;
        }

        template <typename... Args>
        bool PushBack(Args&&... args) {
            auto res = m_allocator.Allocate(sizeof(Node), alignof(Node));
            if (!res.ok()) return false;

            auto* new_node = FoundationKit::ConstructAt<Node>(res.ptr, m_tail, nullptr, FoundationKit::Forward<Args>(args)...);
            if (m_tail) {
                m_tail->next = new_node;
            } else {
                m_head = new_node;
            }
            m_tail = new_node;
            m_size++;
            return true;
        }

        void PopFront() {
            if (!m_head) return;

            Node* old_head = m_head;
            m_head = m_head->next;
            if (m_head) {
                m_head->prev = nullptr;
            } else {
                m_tail = nullptr;
            }

            old_head->~Node();
            m_allocator.Deallocate(old_head, sizeof(Node));
            m_size--;
        }

        void PopBack() {
            if (!m_tail) return;

            Node* old_tail = m_tail;
            m_tail = m_tail->prev;
            if (m_tail) {
                m_tail->next = nullptr;
            } else {
                m_head = nullptr;
            }

            old_tail->~Node();
            m_allocator.Deallocate(old_tail, sizeof(Node));
            m_size--;
        }

        void Clear() {
            while (m_head) {
                PopFront();
            }
        }

        [[nodiscard]] usize Size() const { return m_size; }
        [[nodiscard]] bool Empty() const { return m_size == 0; }

        [[nodiscard]] T& Front() { return m_head->value; }
        [[nodiscard]] const T& Front() const { return m_head->value; }
        [[nodiscard]] T& Back() { return m_tail->value; }
        [[nodiscard]] const T& Back() const { return m_tail->value; }

        Iterator begin() { return Iterator(m_head); }
        Iterator end() { return Iterator(nullptr); }

    private:
        Alloc m_allocator;
        Node* m_head;
        Node* m_tail;
        usize m_size;
    };

} // namespace FoundationKit::Structure
