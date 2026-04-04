#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>

namespace FoundationKitMemory {

    /// @brief An allocator that uses a fixed-size buffer.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<StaticAllocator<Size>, SpinLock>
    template <usize Size>
    class StaticAllocator {
    public:
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            const uptr current = reinterpret_cast<uptr>(m_buffer + m_offset);
            const uptr aligned = (current + align - 1) & ~(align - 1);
            const usize padding = aligned - current;

            if (m_offset + padding + size > Size) return AllocationResult::Failure();

            m_offset += padding;
            void* ptr = m_buffer + m_offset;
            m_offset += size;

            return {ptr, size};
        }

        void Deallocate(void*, usize) noexcept {
            // StaticAllocator doesn't support individual deallocation.
        }

        void DeallocateAll() noexcept {
            m_offset = 0;
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return ptr >= m_buffer && ptr < m_buffer + Size;
        }

    private:
        byte m_buffer[Size]{};
        usize m_offset = 0;
    };

    static_assert(IAllocator<StaticAllocator<1024>>);
    static_assert(IClearable<StaticAllocator<1024>>);

} // namespace FoundationKitMemory
