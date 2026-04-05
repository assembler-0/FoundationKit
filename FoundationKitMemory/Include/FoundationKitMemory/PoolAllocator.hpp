#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveSinglyLinkedList.hpp>

namespace FoundationKitMemory {

    /// @brief A fast fixed-size allocator.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<PoolAllocator<Size>, SpinLock>
    /// @brief A fast fixed-size allocator.
    /// @tparam ChunkSize  Fixed size of objects (0 = use runtime size).
    /// @tparam Alignment  Object alignment (default: 8).
    template <usize ChunkSize = 0, usize Alignment = 8>
    class PoolAllocator {
    public:
        using FreeList = FoundationKitCxxStl::Structure::IntrusiveSinglyLinkedList<void>;
        using Node = typename FreeList::Node;

        constexpr PoolAllocator() noexcept = default;

        /// @brief Initialize the pool with a raw buffer and runtime configuration.
        void Initialize(void* buffer, usize size, usize chunk_size = ChunkSize, usize align = Alignment) noexcept {
            m_buffer      = static_cast<byte*>(buffer);
            m_buffer_size = size;
            m_chunk_size  = chunk_size;
            m_alignment   = align;
            m_free_list.Clear();

            if (!buffer || size == 0 || chunk_size == 0) return;

            if constexpr (ChunkSize != 0) {
                FK_BUG_ON(chunk_size != ChunkSize,
                    "PoolAllocator: runtime chunk_size ({}) mismatch with template ({})",
                    chunk_size, ChunkSize);
            }

            const usize actual_chunk_size = chunk_size < sizeof(Node) ? sizeof(Node) : chunk_size;
            const usize aligned_chunk_size = (actual_chunk_size + align - 1) & ~(align - 1);

            usize offset = 0;
            while (offset + aligned_chunk_size <= size) {
                auto* node = reinterpret_cast<Node*>(m_buffer + offset);
                m_free_list.PushFront(node);
                offset += aligned_chunk_size;
            }
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size > m_chunk_size || align > m_alignment || m_free_list.Empty()) {
                return AllocationResult::Failure();
            }

            Node* node = m_free_list.PopFront();
            return {node, m_chunk_size};
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr || size > m_chunk_size) return;

            auto* node = static_cast<Node*>(ptr);
            m_free_list.PushFront(node);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return ptr >= m_buffer && ptr < m_buffer + m_buffer_size;
        }

        [[nodiscard]] constexpr usize ChunkSizeValue() const noexcept { return m_chunk_size; }

    private:
        FreeList m_free_list;
        byte*    m_buffer      = nullptr;
        usize    m_buffer_size = 0;
        usize    m_chunk_size  = ChunkSize;
        usize    m_alignment   = Alignment;
    };

} // namespace FoundationKitMemory
