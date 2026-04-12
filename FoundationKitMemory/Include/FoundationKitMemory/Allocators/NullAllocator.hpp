#pragma once

namespace FoundationKitMemory {

    /// @brief An allocator that does nothing and always fails to allocate.
    /// @desc Thread-safe: Uses only static methods with no state. No allocations occur.
    class NullAllocator {
    public:
        [[nodiscard]] static constexpr AllocationResult Allocate(usize, usize) noexcept {
            return AllocationResult::Failure();
        }

        static constexpr void Deallocate(void*, usize) noexcept {}

        [[nodiscard]] static constexpr bool Owns(const void*) noexcept {
            return false;
        }
    };

} // namespace FoundationKitMemory
