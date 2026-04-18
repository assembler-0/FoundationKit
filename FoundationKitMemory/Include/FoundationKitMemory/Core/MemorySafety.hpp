#pragma once

#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

/// @brief Paranoid memory safety helpers for FoundationKitMemory.
///        Compile-time checks fire at instantiation; runtime checks use
///        FK_BUG_ON and crash immediately on the first violation.
namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // SafeSizeOf / SafeAlignOf — type-safe sizeof/alignof
    // =========================================================================

    /// @brief sizeof(T) with compile-time rejection of void and incomplete types.
    ///        A plain sizeof(void) compiles silently on some compilers; this does not.
    template <typename T>
    struct SafeSizeOfHelper {
        static_assert(!Void<T>,
            "SafeSizeOf: cannot take sizeof(void)");
        static_assert(!Reference<T>,
            "SafeSizeOf: T must not be a reference — did you mean RemoveReference<T>?");
        static_assert(sizeof(T) > 0,
            "SafeSizeOf: T appears to be an incomplete type");
        static constexpr usize Value = sizeof(T);
    };

    template <typename T>
    inline constexpr usize SafeSizeOf = SafeSizeOfHelper<T>::Value;

    /// @brief alignof(T) with the same guards as SafeSizeOf.
    template <typename T>
    struct SafeAlignOfHelper {
        static_assert(!Void<T>,
            "SafeAlignOf: cannot take alignof(void)");
        static_assert(!Reference<T>,
            "SafeAlignOf: T must not be a reference");
        static_assert((alignof(T) & (alignof(T) - 1)) == 0,
            "SafeAlignOf: alignof(T) must be a power of two (compiler/ABI invariant violated)");
        static constexpr usize Value = alignof(T);
    };

    template <typename T>
    inline constexpr usize SafeAlignOf = SafeAlignOfHelper<T>::Value;

    // =========================================================================
    // MemoryRange — [base, base+size) with overlap/containment checks
    // =========================================================================

    /// @brief Represents a half-open byte range [base, base+size).
    ///        All construction and query methods are paranoid: they crash on
    ///        wraparound, zero-base with non-zero size, and malformed ranges.
    struct MemoryRange {
        uptr base = 0;
        usize size = 0;

        /// @brief Construct from a raw pointer and byte count.
        /// @param ptr  Start of the range. Must not be null if size > 0.
        /// @param sz   Number of bytes. May be zero (empty range).
        [[nodiscard]] static constexpr MemoryRange FromPtr(const void* ptr, usize sz) noexcept {
            if (sz == 0) return {0, 0};
            FK_BUG_ON(ptr == nullptr,
                "MemoryRange::FromPtr: null pointer with non-zero size ({})", sz);
            const uptr b = reinterpret_cast<uptr>(ptr);
            // Detect address-space wraparound: b + sz must not overflow.
            FK_BUG_ON(b + sz < b,
                "MemoryRange::FromPtr: range wraps around address space (base: {}, size: {})", b, sz);
            return {b, sz};
        }

        /// @brief Construct directly from integer base and size.
        [[nodiscard]] static constexpr MemoryRange FromBase(uptr b, usize sz) noexcept {
            if (sz == 0) return {0, 0};
            FK_BUG_ON(b == 0,
                "MemoryRange::FromBase: base is null with non-zero size ({})", sz);
            FK_BUG_ON(b + sz < b,
                "MemoryRange::FromBase: range wraps around address space (base: {}, size: {})", b, sz);
            return {b, sz};
        }

        [[nodiscard]] constexpr uptr End()   const noexcept { return base + size; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return size == 0; }

        /// @brief True if this range fully contains [ptr, ptr+sz).
        [[nodiscard]] constexpr bool Contains(const void* ptr, usize sz) const noexcept {
            if (sz == 0) return true;
            FK_BUG_ON(ptr == nullptr,
                "MemoryRange::Contains: null pointer with non-zero size ({})", sz);
            const uptr p = reinterpret_cast<uptr>(ptr);
            FK_BUG_ON(p + sz < p,
                "MemoryRange::Contains: query range wraps around address space");
            return p >= base && (p + sz) <= End();
        }

        /// @brief True if this range fully contains another MemoryRange.
        [[nodiscard]] constexpr bool Contains(const MemoryRange& other) const noexcept {
            if (other.Empty()) return true;
            return other.base >= base && other.End() <= End();
        }

        /// @brief True if this range overlaps with [ptr, ptr+sz) at all.
        [[nodiscard]] constexpr bool Overlaps(const void* ptr, usize sz) const noexcept {
            if (sz == 0 || Empty()) return false;
            FK_BUG_ON(ptr == nullptr,
                "MemoryRange::Overlaps: null pointer with non-zero size ({})", sz);
            const uptr p = reinterpret_cast<uptr>(ptr);
            FK_BUG_ON(p + sz < p,
                "MemoryRange::Overlaps: query range wraps around address space");
            return p < End() && (p + sz) > base;
        }

        /// @brief True if this range overlaps with another MemoryRange.
        [[nodiscard]] constexpr bool Overlaps(const MemoryRange& other) const noexcept {
            if (other.Empty() || Empty()) return false;
            return other.base < End() && other.End() > base;
        }
    };

    // =========================================================================
    // AssertNoOverlap — crash if two memory regions overlap
    // =========================================================================

    /// @brief Crashes with a detailed message if [a, a+a_size) and [b, b+b_size)
    ///        overlap. Use before any MemoryCopy call or dual-buffer operation
    ///        where overlap would corrupt data silently.
    /// @param a      Start of first region.
    /// @param a_size Byte count of first region.
    /// @param b      Start of second region.
    /// @param b_size Byte count of second region.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    constexpr void AssertNoOverlap(const void* a, usize a_size,
                                   const void* b, usize b_size) noexcept {
        if (a_size == 0 || b_size == 0) return;
        const MemoryRange ra = MemoryRange::FromPtr(a, a_size);
        const MemoryRange rb = MemoryRange::FromPtr(b, b_size);
        FK_BUG_ON(ra.Overlaps(rb),
            "AssertNoOverlap: regions overlap "
            "(a: {} size: {}, b: {} size: {})",
            a, a_size, b, b_size);
    }

    // =========================================================================
    // AssertAllocResultValid — paranoid AllocationResult invariant check
    // =========================================================================

    /// @brief Verifies every invariant of an AllocationResult in one call.
    ///        Call this immediately after receiving a result from an allocator
    ///        you do not fully trust (e.g. a user-supplied allocator, a wrapper
    ///        under test, or after a Reallocate).
    ///
    ///        Invariants checked:
    ///          1. Success ↔ ptr != null AND error == None AND size > 0.
    ///          2. Failure ↔ ptr == null AND error != None AND size == 0.
    ///          3. Returned pointer satisfies requested alignment.
    ///          4. Returned size >= requested size (allocator must not shrink).
    ///
    /// @param result        The result to validate.
    /// @param requested_size The size that was passed to Allocate/Reallocate.
    /// @param requested_align The alignment that was passed.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    constexpr void AssertAllocResultValid(const AllocationResult& result,
                                          usize requested_size,
                                          usize requested_align = 1) noexcept {
        if (result.ptr != nullptr) {
            // Success path invariants.
            FK_BUG_ON(result.error != MemoryError::None,
                "AssertAllocResultValid: non-null ptr but error code is set ({})",
                static_cast<u8>(result.error));
            FK_BUG_ON(result.size == 0,
                "AssertAllocResultValid: non-null ptr but size is zero");
            FK_BUG_ON(result.size < requested_size,
                "AssertAllocResultValid: allocator returned size ({}) < requested ({})",
                result.size, requested_size);

            // Alignment check: the pointer must satisfy the requested alignment.
            // requested_align == 0 is itself a bug.
            FK_BUG_ON(requested_align == 0,
                "AssertAllocResultValid: requested_align is zero (must be >= 1)");
            FK_BUG_ON((requested_align & (requested_align - 1)) != 0,
                "AssertAllocResultValid: requested_align ({}) is not a power of two",
                requested_align);
            FK_BUG_ON((reinterpret_cast<uptr>(result.ptr) & (requested_align - 1)) != 0,
                "AssertAllocResultValid: returned pointer {} is not aligned to {}",
                result.ptr, requested_align);
        } else {
            // Failure path invariants.
            FK_BUG_ON(result.error == MemoryError::None,
                "AssertAllocResultValid: null ptr but error is None (ambiguous failure)");
            FK_BUG_ON(result.size != 0,
                "AssertAllocResultValid: null ptr but size is non-zero ({})", result.size);
        }
    }

    // =========================================================================
    // AssertAligned — crash if a pointer is not aligned
    // =========================================================================

    /// @brief Crashes if ptr is not aligned to align bytes.
    ///        Use before any typed reinterpret_cast to catch misaligned accesses
    ///        that would cause hardware faults on strict-alignment architectures.
    /// @param ptr   The pointer to check.
    /// @param align Required alignment (must be a power of two).
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    constexpr void AssertAligned(const void* ptr, usize align) noexcept {
        FK_BUG_ON(align == 0,
            "AssertAligned: alignment must be >= 1");
        FK_BUG_ON((align & (align - 1)) != 0,
            "AssertAligned: alignment ({}) must be a power of two", align);
        FK_BUG_ON(ptr == nullptr,
            "AssertAligned: pointer is null");
        FK_BUG_ON((reinterpret_cast<uptr>(ptr) & (align - 1)) != 0,
            "AssertAligned: pointer {} is not aligned to {} bytes", ptr, align);
    }

    /// @brief Type-deducing overload: alignment is derived from T automatically.
    template <typename T>
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    constexpr void AssertAlignedFor(const void* ptr) noexcept {
        static_assert(!Void<T>,      "AssertAlignedFor: T must not be void");
        static_assert(!Reference<T>, "AssertAlignedFor: T must not be a reference");
        AssertAligned(ptr, alignof(T));
    }

    // =========================================================================
    // AssertSizeMultiple — crash if size is not a multiple of element_size
    // =========================================================================

    /// @brief Crashes if total_size is not an exact multiple of element_size.
    ///        Use when slicing a raw buffer into typed slots to detect off-by-one
    ///        sizing bugs that would leave a partial object at the end.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    constexpr void AssertSizeMultiple(usize total_size, usize element_size) noexcept {
        FK_BUG_ON(element_size == 0,
            "AssertSizeMultiple: element_size must not be zero");
        FK_BUG_ON(total_size % element_size != 0,
            "AssertSizeMultiple: total_size ({}) is not a multiple of element_size ({})",
            total_size, element_size);
    }

} // namespace FoundationKitMemory
