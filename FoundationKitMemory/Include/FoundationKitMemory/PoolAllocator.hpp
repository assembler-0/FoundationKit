#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveSinglyLinkedList.hpp>

namespace FoundationKitMemory {

    /// @brief A fast fixed-size allocator.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<PoolAllocator<Size>, SpinLock>
    template <usize ChunkSize, usize Alignment = 8>
    class PoolAllocator {
    public:
        // Note: Using IntrusiveSinglyLinkedList from FoundationKitCxxStl
        using FreeList = FoundationKitCxxStl::Structure::IntrusiveSinglyLinkedList<void>;
        using Node = typename FreeList::Node;

        PoolAllocator() noexcept : m_free_list(), m_buffer(nullptr), m_buffer_size(0) {}

        /// @brief Initialize the pool with a raw buffer.
        void Initialize(void* buffer, usize size) noexcept {
            m_buffer = static_cast<byte*>(buffer);
            m_buffer_size = size;
            m_free_list.Clear();

            constexpr usize actual_chunk_size = ChunkSize < sizeof(Node) ? sizeof(Node) : ChunkSize;
            constexpr usize aligned_chunk_size = (actual_chunk_size + Alignment - 1) & ~(Alignment - 1);

            usize offset = 0;
            while (offset + aligned_chunk_size <= size) {
                auto* node = reinterpret_cast<Node*>(m_buffer + offset);
                m_free_list.PushFront(node);
                offset += aligned_chunk_size;
            }
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size > ChunkSize || align > Alignment || m_free_list.Empty()) {
                return AllocationResult::Failure();
            }

            Node* node = m_free_list.PopFront();
            return {node, ChunkSize};
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr || size > ChunkSize) return;

            auto* node = static_cast<Node*>(ptr);
            m_free_list.PushFront(node);
        }

        bool Owns(const void* ptr) const noexcept {
            return ptr >= m_buffer && ptr < m_buffer + m_buffer_size;
        }

    private:
        FreeList m_free_list;
        byte*    m_buffer;
        usize    m_buffer_size;
    };

} // namespace FoundationKitMemory
