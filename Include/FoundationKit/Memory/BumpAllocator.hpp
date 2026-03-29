#pragma once

#include <FoundationKit/Memory/Allocator.hpp>

namespace FoundationKit::Memory {

    /// @brief Fast, arena-style allocator. Only deallocates everything at once via DeallocateAll.
    class BumpAllocator {
    public:
        constexpr BumpAllocator() noexcept = default;
        
        constexpr BumpAllocator(void* start, const usize size) noexcept
            : m_start(static_cast<u8*>(start)), m_current(m_start), m_end(m_start + size) {
            FK_BUG_ON(start == nullptr && size > 0, "BumpAllocator: null start with non-zero size");
        }

        [[nodiscard]] AllocResult Allocate(const usize size, const usize align) noexcept {
            FK_BUG_ON(align == 0 || (align & (align - 1)) != 0, "BumpAllocator: alignment must be a power of two");
            
            const uptr current_ptr = reinterpret_cast<uptr>(m_current);
            const uptr aligned_ptr = (current_ptr + align - 1) & ~(static_cast<uptr>(align) - 1);
            
            // Paranoid check for overflow in aligned_ptr + size
            FK_BUG_ON(aligned_ptr < current_ptr, "BumpAllocator: alignment overflow");
            FK_BUG_ON(aligned_ptr + size < aligned_ptr, "BumpAllocator: allocation size overflow");

            if (aligned_ptr + size > reinterpret_cast<uptr>(m_end)) {
                return AllocResult::failure();
            }

            m_current = reinterpret_cast<u8*>(aligned_ptr + size);
            return { reinterpret_cast<void*>(aligned_ptr), size };
        }

        static constexpr void Deallocate(void*, usize) noexcept {
            // bump allocator cannot free
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
        u8* m_start   = nullptr;
        u8* m_current = nullptr;
        u8* m_end     = nullptr;
    };

    static_assert(IAllocator<BumpAllocator>);
    static_assert(IClearable<BumpAllocator>);

} // namespace FoundationKit::Memory
