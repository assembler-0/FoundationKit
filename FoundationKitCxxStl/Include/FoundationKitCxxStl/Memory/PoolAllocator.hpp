#pragma once

#include <FoundationKitCxxStl/Memory/Allocator.hpp>
#include <FoundationKitCxxStl/Memory/Operations.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveSinglyLinkedList.hpp>

namespace FoundationKitCxxStl::Memory {

    /// @brief A fast fixed-size allocator.
    /// @tparam ChunkSize Size of each allocation chunk.
    /// @tparam Alignment Alignment of each allocation chunk.
    template <usize ChunkSize, usize Alignment = 8>
    class PoolAllocator {
    public:
        using FreeList = Structure::IntrusiveSinglyLinkedList<void>;
        using Node = FreeList::Node;

        PoolAllocator() noexcept : m_free_list(), m_buffer(nullptr), m_buffer_size(0) {}

        /// @brief Initialize the pool with a raw buffer.
        void Initialize(void* buffer, usize size) noexcept {
            m_buffer = static_cast<byte*>(buffer);
            m_buffer_size = size;
            m_free_list.Clear();

            const usize actual_chunk_size = ChunkSize < sizeof(Node) ? sizeof(Node) : ChunkSize;
            const usize aligned_chunk_size = (actual_chunk_size + Alignment - 1) & ~(Alignment - 1);

            usize offset = 0;
            while (offset + aligned_chunk_size <= size) {
                auto* node = reinterpret_cast<Node*>(m_buffer + offset);
                m_free_list.PushFront(node);
                offset += aligned_chunk_size;
            }
        }

        AllocResult Allocate(usize size, usize align) noexcept {
            if (size > ChunkSize || align > Alignment || m_free_list.Empty()) {
                return AllocResult::failure();
            }

            Node* node = m_free_list.PopFront();
            return {node, ChunkSize};
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr || size > ChunkSize) return;

            auto* node = static_cast<Node*>(ptr);
            m_free_list.PushFront(node);
        }

        bool Owns(void* ptr) const noexcept {
            return ptr >= m_buffer && ptr < m_buffer + m_buffer_size;
        }

    private:
        FreeList m_free_list;
        byte*    m_buffer;
        usize    m_buffer_size;
    };

} // namespace FoundationKitCxxStl::Memory
