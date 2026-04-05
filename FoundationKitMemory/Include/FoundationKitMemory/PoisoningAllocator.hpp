#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/MemorySafety.hpp>

namespace FoundationKitMemory {

    /// @brief Security decorator that poisons memory on allocation and deallocation.
    /// @desc Helps detect use-after-free and uninitialized memory usage.
    /// @warning NOT thread-safe. Inherits unsafety from underlying allocator. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<PoisoningAllocator<Alloc>, SpinLock>
    /// @tparam Alloc The underlying allocator type.
    template <IAllocator Alloc>
    class PoisoningAllocator {
    public:
        static constexpr byte PoisonAlloc = 0xDE;
        static constexpr byte PoisonFree  = 0xDF;

        explicit constexpr PoisoningAllocator(Alloc& alloc) noexcept : m_alloc(alloc) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            FK_BUG_ON(size == 0, "PoisoningAllocator::Allocate: zero-size allocation requested");
            FK_BUG_ON(align == 0 || (align & (align - 1)) != 0,
                "PoisoningAllocator::Allocate: alignment ({}) must be a non-zero power of two", align);
            AllocationResult res = m_alloc.Allocate(size, align);
            if (res.ok()) {
                AssertAllocResultValid(res, size, align);
                MemorySet(res.ptr, PoisonAlloc, res.size);
            }
            return res;
        }

        void Deallocate(void* ptr, usize size) noexcept {
            FK_BUG_ON(ptr == nullptr && size > 0,
                "PoisoningAllocator::Deallocate: null pointer with non-zero size ({})", size);
            FK_BUG_ON(ptr != nullptr && !m_alloc.Owns(ptr),
                "PoisoningAllocator::Deallocate: pointer {} does not belong to the underlying allocator", ptr);
            if (ptr && size > 0) {
                MemorySet(ptr, PoisonFree, size);
            }
            m_alloc.Deallocate(ptr, size);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_alloc.Owns(ptr);
        }

        [[nodiscard]] AllocationResult Reallocate(void* ptr, usize old_size, usize new_size, usize align) noexcept {
            // Handle shrink: poison the discarded tail
            if (new_size < old_size && ptr) {
                MemorySet(static_cast<byte*>(ptr) + new_size, PoisonFree, old_size - new_size);
            }

            AllocationResult res;
            if constexpr (IReallocatableAllocator<Alloc>) {
                res = m_alloc.Reallocate(ptr, old_size, new_size, align);
            } else {

                // Manual fallback: Allocate -> Copy -> Deallocate
                if (new_size == 0) {
                    Deallocate(ptr, old_size);
                    return AllocationResult::Failure();
                }

                if (new_size <= old_size) {
                    return AllocationResult::Success(ptr, new_size);
                }

                res = Allocate(new_size, align);
                if (res.ok() && ptr) {
                    MemoryCopy(res.ptr, ptr, old_size);
                    Deallocate(ptr, old_size);
                }
            }

            // Handle grow: poison the new part
            if (res.ok() && new_size > old_size) {
                MemorySet(static_cast<byte*>(res.ptr) + old_size, PoisonAlloc, new_size - old_size);
            }

            return res;
        }

    private:
        Alloc& m_alloc;
    };

    template <IAllocator Alloc>
    PoisoningAllocator(Alloc&) -> PoisoningAllocator<Alloc>;

} // namespace FoundationKitMemory
