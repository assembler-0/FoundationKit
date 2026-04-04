#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryCommon.hpp>

namespace FoundationKitMemory {

    /// @brief A general-purpose allocator that manages variable-sized blocks.
    /// @desc Uses a first-fit strategy with coalescing on deallocation.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<FreeListAllocator, Mutex>  (for high contention)
    class FreeListAllocator {
    public:
        struct Node {
            usize size;
            Node* next;
        };

        struct AllocationHeader {
            u32   magic;   // Safety magic: 0x46524545 ('FREE')
            u32   padding;
            usize size;    // Total block size (including header + padding)
        };

        static constexpr u32 HeaderMagic = 0x46524545;

        constexpr FreeListAllocator() noexcept = default;

        constexpr FreeListAllocator(void* start, usize size) noexcept {
            Initialize(start, size);
        }

        /// @brief Initialize the allocator with a raw memory region.
        void Initialize(void* start, usize size) noexcept {
            if (!start || size < sizeof(Node)) {
                m_start = nullptr;
                m_size = 0;
                m_free_list = nullptr;
                return;
            }

            m_start = static_cast<byte*>(start);
            m_size = size;
            m_free_list = reinterpret_cast<Node*>(m_start);
            m_free_list->size = size;
            m_free_list->next = nullptr;
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size == 0 || !m_free_list) return AllocationResult::Failure();

            Node* prev = nullptr;
            Node* current = m_free_list;

            while (current) {
                const uptr current_ptr = reinterpret_cast<uptr>(current);

                // Calculate padding needed for alignment after the header
                const uptr payload_ptr = Alignment(align).AlignUp(current_ptr + sizeof(AllocationHeader));
                const auto padding = static_cast<u32>(payload_ptr - current_ptr);
                const usize total_needed = padding + size;

                if (current->size >= total_needed) {
                    // Split the block if there's enough space for another Node
                    if (current->size >= total_needed + sizeof(Node)) {
                        Node* next_node = reinterpret_cast<Node*>(current_ptr + total_needed);
                        next_node->size = current->size - total_needed;
                        next_node->next = current->next;

                        if (prev) prev->next = next_node;
                        else m_free_list = next_node;
                    } else {
                        // Use the entire block
                        if (prev) prev->next = current->next;
                        else m_free_list = current->next;
                    }

                    // Store allocation header just before the payload
                    auto* header = reinterpret_cast<AllocationHeader*>(payload_ptr - sizeof(AllocationHeader));
                    header->magic = HeaderMagic;
                    header->padding = padding;
                    header->size = total_needed;

                    return AllocationResult::Success(reinterpret_cast<void*>(payload_ptr), size);
                }

                prev = current;
                current = current->next;
            }

            return AllocationResult::Failure();
        }

        /// @brief Deallocate memory with size (size ignored as it's tracked in header).
        void Deallocate(void* ptr, usize /*size*/) noexcept {
            Deallocate(ptr);
        }

        /// @brief Deallocate memory (size-less).
        void Deallocate(void* ptr) noexcept {
            if (!ptr) return;

            auto* header = reinterpret_cast<AllocationHeader*>(static_cast<byte*>(ptr) - sizeof(AllocationHeader));
            FK_BUG_ON(header->magic != HeaderMagic,
                "FreeListAllocator: Header magic mismatch (expected: {} got: {})", HeaderMagic, header->magic);

            const uptr block_start = reinterpret_cast<uptr>(ptr) - header->padding;
            const usize block_size = header->size;

            Node* new_node = reinterpret_cast<Node*>(block_start);
            new_node->size = block_size;
            new_node->next = nullptr;

            // Insert into free list in address order to facilitate coalescing
            if (!m_free_list || block_start < reinterpret_cast<uptr>(m_free_list)) {
                new_node->next = m_free_list;
                m_free_list = new_node;
            } else {
                Node* current = m_free_list;
                while (current->next && reinterpret_cast<uptr>(current->next) < block_start) {
                    current = current->next;
                }
                new_node->next = current->next;
                current->next = new_node;
            }

            Coalesce();
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return ptr >= m_start && ptr < m_start + m_size;
        }

        /// @brief Combine adjacent free blocks into larger ones.
        void Coalesce() noexcept {
            Node* current = m_free_list;
            while (current && current->next) {
                const uptr current_end = reinterpret_cast<uptr>(current) + current->size;
                if (current_end == reinterpret_cast<uptr>(current->next)) {
                    current->size += current->next->size;
                    current->next = current->next->next;
                } else {
                    current = current->next;
                }
            }
        }

        [[nodiscard]] usize UsedMemory() const noexcept {
            usize free_size = 0;
            Node* current = m_free_list;
            while (current) {
                free_size += current->size;
                current = current->next;
            }
            return m_size - free_size;
        }

    private:
        byte* m_start     = nullptr;
        usize m_size      = 0;
        Node* m_free_list = nullptr;
    };

    static_assert(IAllocator<FreeListAllocator>);

} // namespace FoundationKitMemory
