#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Math.hpp>

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
            FK_BUG_ON(align == 0, "Alignment: cannot be zero");
            FK_BUG_ON((align & (align - 1)) != 0, 
                "Alignment: must be a power of 2 (provided: {})", align);
            FK_BUG_ON(align > (1ULL << 48), 
                "Alignment: value ({}) is suspiciously large (architecture limit check)", align);
        }

        [[nodiscard]] constexpr bool IsPowerOfTwo() const noexcept {
            return value > 0 && (value & (value - 1)) == 0;
        }

        [[nodiscard]] constexpr uptr AlignUp(uptr ptr) const noexcept {
            FK_BUG_ON(value == 0, "Alignment::AlignUp: internal state corruption (value is 0)");
            return FoundationKitCxxStl::AlignUp(ptr, value);
        }

        [[nodiscard]] constexpr uptr AlignDown(uptr ptr) const noexcept {
            FK_BUG_ON(value == 0, "Alignment::AlignDown: internal state corruption (value is 0)");
            return FoundationKitCxxStl::AlignDown(ptr, value);
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
        DesignationMismatch,  // e.g., deleting array as single object
        CorruptionDetected,   // Memory integrity failed
        InvalidAddress,       // E.g., page fault on unmapped area
        AccessViolation,      // E.g., read-only page fault on write
        NotSupported          // Valid but unsupported
    };

    // ============================================================================
    // Allocation Result
    // ============================================================================

    /// @brief Result of an allocation operation.
    struct AllocationResult {
        void*       ptr   = nullptr;
        usize       size  = 0;
        MemoryError error = MemoryError::None;

        [[nodiscard]] constexpr bool IsSuccess() const noexcept {
            return ptr != nullptr && error == MemoryError::None;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsSuccess(); }

        // Backwards compatibility
        [[nodiscard]] constexpr bool ok() const noexcept { return IsSuccess(); }

        /// @brief Create a successful result.
        [[nodiscard]] static constexpr AllocationResult Success(void* p, usize s) noexcept {
            FK_BUG_ON(p == nullptr, "AllocationResult::Success: cannot pass null pointer");
            FK_BUG_ON(s == 0, "AllocationResult::Success: cannot pass zero size");
            return {p, s, MemoryError::None};
        }

        /// @brief Create a failure result with specific error.
        [[nodiscard]] static constexpr AllocationResult Failure(MemoryError err = MemoryError::OutOfMemory) noexcept {
            FK_BUG_ON(err == MemoryError::None, "AllocationResult::Failure: cannot pass MemoryError::None");
            return {nullptr, 0, err};
        }
    };

    // ============================================================================
    // Allocator Traits (capability markers)
    // ============================================================================

    /// @brief Trait: Does the allocator support deallocate without size?
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

    // Forward-declare FragmentationReport so the concepts below can reference it
    // without pulling in FragmentationReport.hpp (which includes allocator headers).
    struct FragmentationReport;

    /// @brief Allocator that exposes a FragmentationReport without external helpers.
    template <typename A>
    concept IIntrospectableAllocator = IAllocator<A> && requires(const A& a) {
        { a.Report() } -> SameAs<FragmentationReport>;
    };

    /// @brief Allocator that participates in the pressure-reclaim protocol.
    /// @desc  Reclaim(bytes) returns the actual number of bytes freed — not the target.
    template <typename A>
    concept IReclaimableAllocator = IAllocator<A> && requires(A& a, usize bytes) {
        { a.Reclaim(bytes) } -> SameAs<usize>;
    };

    // ============================================================================
    // Type-Erased BasicMemoryResource (Virtual-Free)
    // ============================================================================

    /// @brief Type-erased structure of function pointers.
    /// @desc  Replaces the old virtual-class BasicMemoryResource to eliminate
    ///        hosted ABI constraints (__cxa_pure_virtual, vtables, RTTI).
    struct BasicMemoryResource {
        void* ctx = nullptr;

        AllocationResult (*allocate_fn)(void*, usize, usize) noexcept = nullptr;
        void             (*deallocate_size_fn)(void*, void*, usize) noexcept = nullptr;
        void             (*deallocate_fn)(void*, void*) noexcept = nullptr;
        bool             (*owns_fn)(const void*, const void*) noexcept = nullptr;
        AllocationResult (*reallocate_fn)(void*, void*, usize, usize, usize) noexcept = nullptr;
        void             (*deallocate_all_fn)(void*) noexcept = nullptr;
        usize            (*bytes_allocated_fn)(const void*) noexcept = nullptr;

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            return allocate_fn(ctx, size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            deallocate_size_fn(ctx, ptr, size);
        }

        void Deallocate(void* ptr) noexcept {
            deallocate_fn(ctx, ptr);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return owns_fn(ctx, ptr);
        }

        [[nodiscard]] AllocationResult Reallocate(void* ptr, usize old_size, usize new_size, usize align) noexcept {
            return reallocate_fn(ctx, ptr, old_size, new_size, align);
        }

        void DeallocateAll() noexcept {
            deallocate_all_fn(ctx);
        }

        [[nodiscard]] usize BytesAllocated() const noexcept {
            return bytes_allocated_fn(ctx);
        }
    };

    // ============================================================================
    // AnyAllocatorCreator: Type-erasure mechanics
    // ============================================================================

    /// @brief Creates a BasicMemoryResource table for a specific allocator type.
    template <IAllocator Alloc>
    struct AnyAllocatorCreator {
        static AllocationResult Allocate(void* ctx, usize size, usize align) noexcept {
            FK_BUG_ON(size == 0, "AnyAllocatorCreator::Allocate: zero size requested");
            const Alignment a{align};
            auto res = static_cast<Alloc*>(ctx)->Allocate(size, align);
            FK_BUG_ON(res.ptr != nullptr && (reinterpret_cast<uptr>(res.ptr) % align) != 0,
                "AnyAllocatorCreator::Allocate: underlying allocator returned unaligned pointer");
            return res;
        }

        static void DeallocateSize(void* ctx, void* ptr, usize size) noexcept {
            FK_BUG_ON(ptr == nullptr && size > 0, "AnyAllocatorCreator::DeallocateSize: null pointer with size");
            static_cast<Alloc*>(ctx)->Deallocate(ptr, size);
        }

        static void Deallocate(void* ctx, void* ptr) noexcept {
            if constexpr (requires { static_cast<Alloc*>(ctx)->Deallocate(ptr); }) {
                static_cast<Alloc*>(ctx)->Deallocate(ptr);
            } else {
                FK_BUG_ON(ptr != nullptr, "AnyAllocatorCreator::Deallocate: unsized deallocate requested but not supported");
                static_cast<Alloc*>(ctx)->Deallocate(ptr, 0);
            }
        }

        static bool Owns(const void* ctx, const void* ptr) noexcept {
            return static_cast<const Alloc*>(ctx)->Owns(ptr);
        }

        static AllocationResult Reallocate(void* ctx, void* ptr, usize old_size, usize new_size, usize align) noexcept {
            FK_BUG_ON(ptr == nullptr && old_size > 0, "AnyAllocatorCreator::Reallocate: null pointer with non-zero old_size");
            if constexpr (IReallocatableAllocator<Alloc>) {
                return static_cast<Alloc*>(ctx)->Reallocate(ptr, old_size, new_size, align);
            } else {
                if (new_size == 0) {
                    DeallocateSize(ctx, ptr, old_size);
                    return AllocationResult::Failure();
                }
                if (new_size <= old_size) {
                    return {ptr, new_size, MemoryError::None};
                }
                AllocationResult new_alloc = Allocate(ctx, new_size, align);
                if (!new_alloc) return new_alloc;
                if (ptr) {
                    auto* src = static_cast<const byte*>(ptr);
                    auto* dst = static_cast<byte*>(new_alloc.ptr);
                    FK_BUG_ON(src == dst, "AnyAllocatorCreator::Reallocate: allocate returned same pointer");
                    for (usize i = 0; i < old_size; ++i) dst[i] = src[i];
                    DeallocateSize(ctx, ptr, old_size);
                }
                return new_alloc;
            }
        }

        static void DeallocateAll(void* ctx) noexcept {
            if constexpr (IClearableAllocator<Alloc>) {
                static_cast<Alloc*>(ctx)->DeallocateAll();
            }
        }

        static usize BytesAllocated(const void* ctx) noexcept {
            if constexpr (IStatefulAllocator<Alloc>) {
                return static_cast<const Alloc*>(ctx)->BytesAllocated();
            }
            return 0;
        }

        static BasicMemoryResource Create(Alloc& a) noexcept {
            return BasicMemoryResource{
                .ctx = &a,
                .allocate_fn = &Allocate,
                .deallocate_size_fn = &DeallocateSize,
                .deallocate_fn = &Deallocate,
                .owns_fn = &Owns,
                .reallocate_fn = &Reallocate,
                .deallocate_all_fn = &DeallocateAll,
                .bytes_allocated_fn = &BytesAllocated
            };
        }
    };

    // ============================================================================
    // Wrapper Helper (replaces AllocatorWrapper<T>)
    // ============================================================================

    template <IAllocator Alloc>
    [[nodiscard]] BasicMemoryResource MakeMemoryResource(Alloc& alloc) noexcept {
        return AnyAllocatorCreator<Alloc>::Create(alloc);
    }

} // namespace FoundationKitMemory
