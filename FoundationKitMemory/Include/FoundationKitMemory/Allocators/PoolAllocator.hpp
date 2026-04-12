#pragma once

#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitMemory/Core/MemorySafety.hpp>
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

            // Alignment must be a power of two.
            FK_BUG_ON((align & (align - 1)) != 0,
                "PoolAllocator::Initialize: alignment ({}) must be a power of two", align);
            // chunk_size must be large enough to hold the free-list pointer.
            FK_BUG_ON(chunk_size < sizeof(void*),
                "PoolAllocator::Initialize: chunk_size ({}) is smaller than sizeof(void*) ({})",
                chunk_size, sizeof(void*));
            // Wraparound guard.
            FK_BUG_ON(reinterpret_cast<uptr>(buffer) + size < reinterpret_cast<uptr>(buffer),
                "PoolAllocator::Initialize: buffer range wraps around address space");

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
                // Zero the node before pushing: the raw buffer contains garbage,
                // so node->next would be non-null and trigger the already-linked
                // check in PushFront. Zeroing makes the invariant hold correctly.
                node->next = nullptr;
                m_free_list.PushFront(node);
                offset += aligned_chunk_size;
            }
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            FK_BUG_ON(size == 0, "PoolAllocator::Allocate: zero-size allocation requested");
            FK_BUG_ON(m_chunk_size == 0,
                "PoolAllocator::Allocate: called before Initialize() (chunk_size is zero)");
            if (size > m_chunk_size || align > m_alignment || m_free_list.Empty()) {
                return AllocationResult::Failure();
            }

            Node* node = m_free_list.PopFront();
            return AllocationResult::Success(node, m_chunk_size);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr) return;
            FK_BUG_ON(!Owns(ptr),
                "PoolAllocator::Deallocate: pointer {} does not belong to this pool", ptr);
            FK_BUG_ON(size > m_chunk_size,
                "PoolAllocator::Deallocate: size ({}) exceeds chunk_size ({})", size, m_chunk_size);
            auto* node = static_cast<Node*>(ptr);
            // The user may have written arbitrary data into this chunk during its
            // lifetime, leaving node->next as garbage. Zero it before PushFront
            // so the already-linked invariant check does not false-positive.
            node->next = nullptr;
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
