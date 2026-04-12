#pragma once


#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitMemory/Core/MemorySafety.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    /// @brief Fast, arena-style allocator. Only deallocates everything at once via DeallocateAll.
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<BumpAllocator, SpinLock>
    class BumpAllocator {
    public:
        constexpr BumpAllocator() noexcept = default;
        
        constexpr BumpAllocator(void* start, const usize size) noexcept
            : m_start(static_cast<byte*>(start)), m_current(m_start), m_end(m_start + size) {
            FK_BUG_ON(start == nullptr && size > 0, "BumpAllocator: null start with non-zero size ({})", size);
            // Wraparound: m_start + size must not overflow.
            FK_BUG_ON(reinterpret_cast<uptr>(m_start) + size < reinterpret_cast<uptr>(m_start),
                "BumpAllocator: buffer range wraps around address space (start: {}, size: {})", start, size);
        }

        [[nodiscard]] AllocationResult Allocate(const usize size, const usize align) noexcept {
            FK_BUG_ON(align == 0 || (align & (align - 1)) != 0, "BumpAllocator: alignment must be a power of two");
            FK_BUG_ON(size == 0, "BumpAllocator: zero-size allocation requested");
            FK_BUG_ON(m_start == nullptr, "BumpAllocator: Allocate called on uninitialised allocator");
            
            const uptr current_ptr = reinterpret_cast<uptr>(m_current);
            const uptr aligned_ptr = (current_ptr + align - 1) & ~(static_cast<uptr>(align) - 1);
            
            FK_BUG_ON(aligned_ptr < current_ptr,
                "BumpAllocator: alignment overflow (aligned: {}) < (current: {})", aligned_ptr, current_ptr);
            
            if (aligned_ptr + size > reinterpret_cast<uptr>(m_end)) {
                return AllocationResult::Failure();
            }

            m_current = reinterpret_cast<byte*>(aligned_ptr + size);
            return AllocationResult::Success(reinterpret_cast<void*>(aligned_ptr), size);
        }

        static constexpr void Deallocate(void*, usize) noexcept {
            // BumpAllocator cannot free individual blocks.
        }

        [[nodiscard]] constexpr bool Owns(const void* ptr) const noexcept {
            return ptr >= m_start && ptr < m_end;
        }

        constexpr void DeallocateAll() noexcept {
            m_current = m_start;
        }

        [[nodiscard]] constexpr usize Remaining() const noexcept {
            return static_cast<usize>(m_end - m_current);
        }

    private:
        byte* m_start   = nullptr;
        byte* m_current = nullptr;
        byte* m_end     = nullptr;
    };

    static_assert(IAllocator<BumpAllocator>);
    static_assert(IClearableAllocator<BumpAllocator>);

} // namespace FoundationKitMemory
