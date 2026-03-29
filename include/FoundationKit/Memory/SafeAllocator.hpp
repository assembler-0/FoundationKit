#pragma once

#include <FoundationKit/Memory/Allocator.hpp>

namespace FoundationKit::Memory {

    /// @brief Wraps an allocator and adds canaries around each allocation to detect overflows.
    template <IAllocator A, usize CanarySize = 16>
    class SafeAllocator : public A {
    public:
        using Underlying = A;

        constexpr SafeAllocator() noexcept = default;

        explicit constexpr SafeAllocator(A& alloc) noexcept : A(FoundationKit::Move(alloc)) {}
        explicit constexpr SafeAllocator(A&& alloc) noexcept : A(FoundationKit::Move(alloc)) {}

        [[nodiscard]] AllocResult Allocate(const usize size, usize align) noexcept {
            usize total_size = size + CanarySize * 2;
            const AllocResult res = Underlying::Allocate(total_size, align);
            
            if (!res) return res;

            const auto head = static_cast<u8*>(res.ptr);
            u8* user_ptr = head + CanarySize;
            u8* tail = user_ptr + size;

            for (usize i = 0; i < CanarySize; ++i) {
                head[i] = 0xDE; // Deadbeef-ish
                tail[i] = 0xAD;
            }

            return { user_ptr, size };
        }

        constexpr void Deallocate(void* ptr, const usize size) noexcept {
            if (!ptr) return;

            const auto user_ptr = static_cast<u8*>(ptr);
            u8* head = user_ptr - CanarySize;
            const u8* tail = user_ptr + size;

            for (usize i = 0; i < CanarySize; ++i) {
                if (head[i] != 0xDE || tail[i] != 0xAD) {
                    FOUNDATIONKIT_UNREACHABLE();
                }
            }

            Underlying::Deallocate(head, size + CanarySize * 2);
        }

        [[nodiscard]] constexpr bool Owns(void* ptr) const noexcept {
            if (!ptr) return false;
            return Underlying::Owns(static_cast<u8*>(ptr) - CanarySize);
        }
    };

    static_assert(IAllocator<SafeAllocator<BumpAllocator>>);

} // namespace FoundationKit::Memory
