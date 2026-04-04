#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    /// @brief A simple singly linked list.
    /// @tparam T The type of elements.
    /// @tparam Alloc The allocator to use for nodes.
    template <typename T, FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class SinglyLinkedList {
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

            bool operator==(const Iterator& other) const { return m_node == other.m_node; }
            bool operator!=(const Iterator& other) const { return m_node != other.m_node; }

            Node* GetNode() const { return m_node; }

        private:
            Node* m_node;
        };

        explicit SinglyLinkedList(Alloc allocator = Alloc())
            : m_allocator(FoundationKitCxxStl::Move(allocator)), m_head(nullptr), m_size(0) {}

        ~SinglyLinkedList() { Clear(); }

        SinglyLinkedList(const SinglyLinkedList&) = delete;
        SinglyLinkedList& operator=(const SinglyLinkedList&) = delete;

        SinglyLinkedList(SinglyLinkedList&& other) noexcept
            : m_allocator(FoundationKitCxxStl::Move(other.m_allocator)), m_head(other.m_head), m_size(other.m_size) {
            other.m_head = nullptr;
            other.m_size = 0;
        }

        SinglyLinkedList& operator=(SinglyLinkedList&& other) noexcept {
            if (this != &other) {
                Clear();
                m_allocator = FoundationKitCxxStl::Move(other.m_allocator);
                m_head = other.m_head;
                m_size = other.m_size;
                other.m_head = nullptr;
                other.m_size = 0;
            }
            return *this;
        }

        template <typename... Args>
        bool PushFront(Args&&... args) {
            auto res = m_allocator.Allocate(sizeof(Node), alignof(Node));
            if (!res.ok()) return false;

            m_head = FoundationKitCxxStl::ConstructAt<Node>(res.ptr, m_head, FoundationKitCxxStl::Forward<Args>(args)...);
            m_size++;
            return true;
        }

        void PopFront() {
            if (!m_head) return;

            Node* old_head = m_head;
            m_head = m_head->next;
            old_head->~Node();
            m_allocator.Deallocate(old_head, sizeof(Node));
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

        Iterator begin() { return Iterator(m_head); }
        Iterator end() { return Iterator(nullptr); }

    private:
        Alloc m_allocator;
        Node* m_head;
        usize m_size;
    };

} // namespace FoundationKitCxxStl::Structure
