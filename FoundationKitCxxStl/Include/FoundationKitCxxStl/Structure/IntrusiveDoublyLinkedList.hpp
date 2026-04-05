#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl::Structure {

    /// @brief A Linux-style intrusive doubly linked list node.
    struct IntrusiveDoublyLinkedListNode {
        IntrusiveDoublyLinkedListNode* next;
        IntrusiveDoublyLinkedListNode* prev;

        constexpr IntrusiveDoublyLinkedListNode() noexcept : next(this), prev(this) {}

        [[nodiscard]] constexpr bool IsShared() const noexcept {
            return next != this;
        }
    };

    /// @brief Helper to get the container pointer from a list node.
    template <typename T, IntrusiveDoublyLinkedListNode T::*Member>
    [[nodiscard]] T* ContainerOf(IntrusiveDoublyLinkedListNode* node) noexcept {
        if (!node) return nullptr;
        
        // Use a dummy object to calculate the offset of the member.
        // This is a common pattern in intrusive lists.
        const T* const dummy = reinterpret_cast<const T*>(0x1000); // Use a non-null address to avoid some UB checks
        const uptr dummy_addr = reinterpret_cast<uptr>(dummy);
        const uptr member_addr = reinterpret_cast<uptr>(&(dummy->*Member));
        const usize offset = member_addr - dummy_addr;
        
        return reinterpret_cast<T*>(reinterpret_cast<uptr>(node) - offset);
    }

    /// @brief A circular intrusive doubly linked list.
    class IntrusiveDoublyLinkedList {
    public:
        constexpr IntrusiveDoublyLinkedList() noexcept = default;

        void PushFront(IntrusiveDoublyLinkedListNode* node) noexcept {
            FK_BUG_ON(node == nullptr, "IntrusiveDoublyLinkedList::PushFront: null node");
            // Detect inserting a node that is already linked into a list.
            // An unlinked node always points to itself (see constructor).
            FK_BUG_ON(node->IsShared(),
                "IntrusiveDoublyLinkedList::PushFront: node is already linked into a list (use-after-insert or missing Remove)");
            Insert(node, &m_head, m_head.next);
            m_size++;
        }

        void PushBack(IntrusiveDoublyLinkedListNode* node) noexcept {
            FK_BUG_ON(node == nullptr, "IntrusiveDoublyLinkedList::PushBack: null node");
            FK_BUG_ON(node->IsShared(),
                "IntrusiveDoublyLinkedList::PushBack: node is already linked into a list (use-after-insert or missing Remove)");
            Insert(node, m_head.prev, &m_head);
            m_size++;
        }

        IntrusiveDoublyLinkedListNode* PopFront() noexcept {
            FK_BUG_ON(Empty(), "IntrusiveDoublyLinkedList: PopFront called on empty list");
            if (Empty()) return nullptr;
            IntrusiveDoublyLinkedListNode* node = m_head.next;
            Remove(node);
            return node;
        }

        IntrusiveDoublyLinkedListNode* PopBack() noexcept {
            FK_BUG_ON(Empty(), "IntrusiveDoublyLinkedList: PopBack called on empty list");
            if (Empty()) return nullptr;
            IntrusiveDoublyLinkedListNode* node = m_head.prev;
            Remove(node);
            return node;
        }

        void Remove(IntrusiveDoublyLinkedListNode* node) noexcept {
            FK_BUG_ON(!node, "IntrusiveDoublyLinkedList: Remove called with null node");
            // Removing an already-unlinked node (next == self) is a double-remove bug.
            FK_BUG_ON(!node->IsShared(),
                "IntrusiveDoublyLinkedList::Remove: node is not linked (double-remove or remove of uninserted node)");
            // Sanity: the size counter must be consistent with the list having nodes.
            FK_BUG_ON(m_size == 0,
                "IntrusiveDoublyLinkedList::Remove: size is zero but a linked node was found (counter corruption)");
            node->prev->next = node->next;
            node->next->prev = node->prev;
            // Reset to self-loop so IsShared() correctly returns false after removal.
            node->next = node;
            node->prev = node;
            m_size--;
        }

        [[nodiscard]] bool Empty() const noexcept { return m_head.next == &m_head; }
        [[nodiscard]] usize Size() const noexcept { return m_size; }

        [[nodiscard]] IntrusiveDoublyLinkedListNode* Begin() const noexcept { return m_head.next; }
        [[nodiscard]] IntrusiveDoublyLinkedListNode* End() noexcept { return &m_head; }

    private:
        static void Insert(IntrusiveDoublyLinkedListNode* node, IntrusiveDoublyLinkedListNode* prev, IntrusiveDoublyLinkedListNode* next) noexcept {
            next->prev = node;
            node->next = next;
            node->prev = prev;
            prev->next = node;
        }

        IntrusiveDoublyLinkedListNode m_head;
        usize m_size = 0;
    };

} // namespace FoundationKitCxxStl::Structure
