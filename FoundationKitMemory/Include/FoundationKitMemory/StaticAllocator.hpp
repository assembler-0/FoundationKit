#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/MemorySafety.hpp>

namespace FoundationKitMemory {

    /// @brief An allocator that uses a fixed-size buffer.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<StaticAllocator<Size>, SpinLock>
    template <usize Size>
    class StaticAllocator {
        static_assert(Size > 0, "StaticAllocator: Size must be greater than zero");
    public:
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            FK_BUG_ON(size == 0, "StaticAllocator::Allocate: zero-size allocation requested");
            FK_BUG_ON(align == 0 || (align & (align - 1)) != 0,
                "StaticAllocator::Allocate: alignment ({}) must be a non-zero power of two", align);

            const uptr current = reinterpret_cast<uptr>(m_buffer + m_offset);
            const uptr aligned = (current + align - 1) & ~(align - 1);
            const usize padding = aligned - current;

            // Overflow check: padding + size must not wrap usize.
            FK_BUG_ON(padding > static_cast<usize>(-1) - size,
                "StaticAllocator::Allocate: padding+size overflow (padding: {}, size: {})", padding, size);

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
    static_assert(IClearableAllocator<StaticAllocator<1024>>);

} // namespace FoundationKitMemory
