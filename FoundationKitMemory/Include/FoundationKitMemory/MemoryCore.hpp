#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Alignment Type
    // ============================================================================

    /// @brief Type-safe alignment wrapper.
    /// @desc Ensures alignment is always a power of 2.
    struct Alignment {
        usize value;

        constexpr Alignment(usize align = 1) noexcept : value(align) {
            FK_BUG_ON(align == 0 || (align & (align - 1)) != 0, 
                "Alignment must be a power of 2");
        }

        [[nodiscard]] constexpr bool IsPowerOfTwo() const noexcept {
            return value > 0 && (value & (value - 1)) == 0;
        }

        [[nodiscard]] constexpr uptr AlignUp(uptr ptr) const noexcept {
            return (ptr + value - 1) & ~(value - 1);
        }

        [[nodiscard]] constexpr uptr AlignDown(uptr ptr) const noexcept {
            return ptr & ~(value - 1);
        }
    };

    constexpr Alignment DefaultAlignment{1};

    // ============================================================================
    // Memory Errors
    // ============================================================================

    /// @brief Enumeration of possible memory errors.
    enum class MemoryError : u8 {
        None = 0,
        OutOfMemory,
        InvalidAlignment,
        InvalidSize,
        NotOwned,
        AllocationTooLarge,
        DesignationMismatch  // e.g., deleting array as single object
    };

    // ============================================================================
    // Allocation Result
    // ============================================================================

    /// @brief Result of an allocation operation.
    struct AllocationResult {
        void*       ptr   = nullptr;
        usize       size  = 0;
        MemoryError error = MemoryError::None;

        [[nodiscard]] constexpr bool IsSuccess() const noexcept { return ptr != nullptr && error == MemoryError::None; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsSuccess(); }

        // Backwards compatibility
        [[nodiscard]] constexpr bool ok() const noexcept { return IsSuccess(); }

        /// @brief Create a successful result.
        [[nodiscard]] static constexpr AllocationResult Success(void* p, usize s) noexcept {
            return {p, s, MemoryError::None};
        }

        /// @brief Create a failure result with specific error.
        [[nodiscard]] static constexpr AllocationResult Failure(MemoryError err = MemoryError::OutOfMemory) noexcept {
            return {nullptr, 0, err};
        }
    };

    // ============================================================================
    // Allocator Traits (capability markers)
    // ============================================================================

    /// @brief Trait: Does the allocator support deallocate without size?
    /// @desc If true, Deallocate(ptr) with no size is safe. If false, use TrackingAllocator.
    template <typename A>
    inline constexpr bool SupportsUnsizedDelete = false;

    /// @brief Trait: Does the allocator support reallocation?
    template <typename A>
    inline constexpr bool SupportsReallocation = false;

    /// @brief Trait: Does the allocator support clearing all at once?
    template <typename A>
    inline constexpr bool SupportsClearAll = false;

    /// @brief Trait: Does the allocator track ownership via Owns()?
    template <typename A>
    inline constexpr bool SupportsOwnershipCheck = false;

    // ============================================================================
    // Core IAllocator Concept
    // ============================================================================

    /// @brief Core allocator protocol all memory providers must satisfy.
    /// @desc Allocators can be either:
    ///       - Owned: They track ownership (Owns returns meaningful result)
    ///       - Unowned: They don't (Owns always returns false or doesn't exist)
    template <typename A>
    concept IAllocator = requires(A& alloc, const void* ptr, void* mut_ptr, usize size, usize align) {
        /// @brief Allocate 'size' bytes with 'align' alignment.
        { alloc.Allocate(size, align) } -> SameAs<AllocationResult>;

        /// @brief Deallocate memory previously allocated (size required).
        { alloc.Deallocate(mut_ptr, size) } -> SameAs<void>;

        /// @brief Check if this allocator owns the pointer (accepts const ptr).
        { alloc.Owns(ptr) } -> SameAs<bool>;
    };

    /// @brief Extended allocator that supports reallocation.
    template <typename A>
    concept IReallocatableAllocator = IAllocator<A> && requires(A& alloc, void* ptr, usize old_size, usize new_size, usize align) {
        { alloc.Reallocate(ptr, old_size, new_size, align) } -> SameAs<AllocationResult>;
    };

    /// @brief Extended allocator that supports clearing all allocations at once.
    template <typename A>
    concept IClearableAllocator = IAllocator<A> && requires(A& alloc) {
        { alloc.DeallocateAll() } -> SameAs<void>;
    };

    /// @brief Extended allocator that reports allocation statistics.
    template <typename A>
    concept IStatefulAllocator = IAllocator<A> && requires(A& alloc) {
        { alloc.BytesAllocated() } -> SameAs<usize>;
        { alloc.BytesDeallocated() } -> SameAs<usize>;
        { alloc.TotalAllocations() } -> SameAs<usize>;
    };

    // ============================================================================
    // Runtime Polymorphic Allocator (RTTI-free virtual interface)
    // ============================================================================

    /// @brief Base class for type-erased allocators (no virtual destructor overhead if not needed).
    /// @desc This is used when you need runtime polymorphism but can't use templates.
    struct BasicMemoryResource {
        /// @brief Virtual destructor for cleanup (optional, can be omitted if no cleanup needed).
        virtual ~BasicMemoryResource() = default;

        /// @brief Allocate 'size' bytes with 'align' alignment.
        [[nodiscard]] virtual AllocationResult Allocate(usize size, usize align) noexcept = 0;

        /// @brief Deallocate memory previously allocated (size required).
        virtual void Deallocate(void* ptr, usize size) noexcept = 0;

        /// @brief Deallocate memory previously allocated (size not required).
        /// @desc Only works if the underlying allocator tracks sizes (e.g., TrackingAllocator).
        ///       Default implementation calls Deallocate(ptr, 0), which may fail or bug.
        virtual void Deallocate(void* ptr) noexcept {
            Deallocate(ptr, 0);
        }

        /// @brief Check if this resource owns the pointer.
        [[nodiscard]] virtual bool Owns(const void* ptr) const noexcept = 0;

        /// @brief Optional reallocation support (default falls back to alloc+copy+free).
        [[nodiscard]] virtual AllocationResult Reallocate(void* ptr, usize old_size, usize new_size, usize align) noexcept {
            if (new_size == 0) {
                Deallocate(ptr, old_size);
                return AllocationResult::Failure();
            }

            if (new_size <= old_size) {
                return {ptr, new_size};
            }

            AllocationResult new_alloc = Allocate(new_size, align);
            if (!new_alloc) return new_alloc;

            if (ptr) {
                // Perform in-place copy (simple byte copy)
                auto* src = static_cast<const byte*>(ptr);
                auto* dst = static_cast<byte*>(new_alloc.ptr);
                for (usize i = 0; i < old_size; ++i) {
                    dst[i] = src[i];
                }
                Deallocate(ptr, old_size);
            }
            return new_alloc;
        }

        /// @brief Optional: Clear all allocations at once (only if allocator supports it).
        virtual void DeallocateAll() noexcept {}

        /// @brief Optional: Report allocated bytes (only if allocator tracks this).
        [[nodiscard]] virtual usize BytesAllocated() const noexcept { return 0; }
    };

    // ============================================================================
    // Wrapper: Concept Allocator -> BasicMemoryResource
    // ============================================================================

    /// @brief Converts any concept-based allocator into a type-erased BasicMemoryResource.
    /// @tparam Alloc Must satisfy IAllocator<Alloc>
    template <IAllocator Alloc>
    struct AllocatorWrapper final : BasicMemoryResource {
        Alloc& allocator;

        explicit constexpr AllocatorWrapper(Alloc& alloc) noexcept : allocator(alloc) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept override {
            return allocator.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept override {
            allocator.Deallocate(ptr, size);
        }

        void Deallocate(void* ptr) noexcept override {
            if constexpr (requires { allocator.Deallocate(ptr); }) {
                allocator.Deallocate(ptr);
            } else {
                allocator.Deallocate(ptr, 0);
            }
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept override {
            return allocator.Owns(ptr);
        }

        [[nodiscard]] AllocationResult Reallocate(void* ptr, usize old_size, usize new_size, usize align) noexcept override {
            if constexpr (IReallocatableAllocator<Alloc>) {
                return allocator.Reallocate(ptr, old_size, new_size, align);
            } else {
                return BasicMemoryResource::Reallocate(ptr, old_size, new_size, align);
            }
        }

        void DeallocateAll() noexcept override {
            if constexpr (IClearableAllocator<Alloc>) {
                allocator.DeallocateAll();
            }
        }

        [[nodiscard]] usize BytesAllocated() const noexcept override {
            if constexpr (IStatefulAllocator<Alloc>) {
                return allocator.BytesAllocated();
            }
            return 0;
        }
    };

    // ============================================================================
    // Backwards Compatibility Aliases
    // ============================================================================

    /// @brief Alias: AllocResult is now AllocationResult.
    using AllocResult = AllocationResult;

    /// @brief Alias: IMemoryResource is now BasicMemoryResource.
    using IMemoryResource = BasicMemoryResource;

    /// @brief Alias: Compatibility wrapper name.
    template <IAllocator Alloc>
    using MemoryResourceWrapper = AllocatorWrapper<Alloc>;

    /// @brief Alias: IReallocatable is now IReallocatableAllocator.
    template <typename A>
    inline constexpr bool IReallocatable = IReallocatableAllocator<A>;

    /// @brief Alias: IClearable is now IClearableAllocator.
    template <typename A>
    inline constexpr bool IClearable = IClearableAllocator<A>;

} // namespace FoundationKitMemory
