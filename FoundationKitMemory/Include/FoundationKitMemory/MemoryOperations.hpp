#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryCommon.hpp>
#include <FoundationKitMemory/MemorySafety.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Memory Movement Operations
    // ============================================================================

    /// @brief Copy memory from 'src' to 'dest'.
    /// @note Does not handle overlapping buffers. Use MemoryMove if buffers overlap.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void* MemoryCopy(void* dest, const void* src, usize size) noexcept {
        if (size == 0) return dest;
        FK_BUG_ON(!dest || !src, "MemoryCopy: null pointer provided with non-zero size ({})", size);
        
        auto* d = static_cast<byte*>(dest);
        const auto* s = static_cast<const byte*>(src);

        // Paranoid: Check for address space wraparound
        FK_BUG_ON(reinterpret_cast<uptr>(d) + size < reinterpret_cast<uptr>(d), 
            "MemoryCopy: destination range wraparound (dest: {}, size: {})", dest, size);
        FK_BUG_ON(reinterpret_cast<uptr>(s) + size < reinterpret_cast<uptr>(s), 
            "MemoryCopy: source range wraparound (src: {}, size: {})", src, size);

        // Paranoid: Strictly forbid overlap in MemoryCopy
        const bool overlap = (d < s + size) && (s < d + size);
        FK_BUG_ON(overlap, "MemoryCopy: overlapping buffers detected! Use MemoryMove instead (dest: {}, src: {}, size: {})", 
            dest, src, size);

#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
        if (FoundationKitOsl::OslIsSimdEnabled()) {
            Base::CompilerBuiltins::MemCpy(dest, src, size);
            return dest;
        }
#endif

        while (size--) *d++ = *s++;
        return dest;
    }

    /// @brief Move memory from 'src' to 'dest', handling overlaps.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void* MemoryMove(void* dest, const void* src, usize size) noexcept {
        if (size == 0) return dest;
        FK_BUG_ON(!dest || !src, "MemoryMove: null pointer provided with non-zero size");

        // Paranoid: Check for address space wraparound
        FK_BUG_ON(reinterpret_cast<uptr>(dest) + size < reinterpret_cast<uptr>(dest), 
            "MemoryMove: destination range wraparound (dest: {}, size: {})", dest, size);
        FK_BUG_ON(reinterpret_cast<uptr>(src) + size < reinterpret_cast<uptr>(src), 
            "MemoryMove: source range wraparound (src: {}, size: {})", src, size);

#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
        if (FoundationKitOsl::OslIsSimdEnabled()) {
            Base::CompilerBuiltins::MemMove(dest, src, size);
            return dest;
        }
#endif

        auto* d = static_cast<byte*>(dest);
        const auto* s = static_cast<const byte*>(src);
        if (d < s) {
            while (size--) *d++ = *s++;
        } else if (d > s) {
            d += size;
            s += size;
            while (size--) *--d = *--s;
        }
        return dest;
    }

    /// @brief Set memory in 'dest' to 'value'.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void* MemorySet(void* dest, const byte value, usize size) noexcept {
        if (size == 0) return dest;
        FK_BUG_ON(!dest, "MemorySet: null pointer provided with non-zero size ({})", size);

        // Paranoid: Check for address space wraparound
        FK_BUG_ON(reinterpret_cast<uptr>(dest) + size < reinterpret_cast<uptr>(dest), 
            "MemorySet: range wraparound (dest: {}, size: {})", dest, size);

#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
        if (FoundationKitOsl::OslIsSimdEnabled()) {
            Base::CompilerBuiltins::MemSet(dest, value, size);
            return dest;
        }
#endif

        auto* d = static_cast<byte*>(dest);
        while (size--) *d++ = value;
        return dest;
    }

    /// @brief Compare memory between 'lhs' and 'rhs'.
    /// @return Negative if lhs < rhs, positive if lhs > rhs, zero if equal.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE i32 MemoryCompare(const void* lhs, const void* rhs, usize size) noexcept {
        if (size == 0) return 0;
        FK_BUG_ON(!lhs || !rhs, "MemoryCompare: null pointer provided with non-zero size ({})", size);

        // Paranoid: Check for address space wraparound
        FK_BUG_ON(reinterpret_cast<uptr>(lhs) + size < reinterpret_cast<uptr>(lhs), 
            "MemoryCompare: lhs range wraparound (lhs: {}, size: {})", lhs, size);
        FK_BUG_ON(reinterpret_cast<uptr>(rhs) + size < reinterpret_cast<uptr>(rhs), 
            "MemoryCompare: rhs range wraparound (rhs: {}, size: {})", rhs, size);

#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
        if (FoundationKitOsl::OslIsSimdEnabled()) {
            return Base::CompilerBuiltins::MemCmp(lhs, rhs, size);
        }
#endif

        const auto* l = static_cast<const byte*>(lhs);
        const auto* r = static_cast<const byte*>(rhs);
        while (size--) {
            if (*l != *r) return static_cast<i32>(*l) - static_cast<i32>(*r);
            l++; r++;
        }
        return 0;
    }

    /// @brief Zero out memory in 'dest'.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void* MemoryZero(void* dest, const usize size) noexcept {
        return MemorySet(dest, 0, size);
    }

    // ============================================================================
    // Object Construction/Destruction Helpers
    // ============================================================================

    /// @brief Construct an object, returning an Optional pointer.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]]
    Optional<T*> New(Alloc& alloc, Args&&... args) noexcept {
        constexpr usize sz = SafeSizeOf<T>;
        constexpr usize al = SafeAlignOf<T>;
        AllocationResult r = alloc.Allocate(sz, al);
        if (!r) return NullOpt;
        AssertAllocResultValid(r, sz, al);
        // Runtime alignment check on the returned pointer before placement-new.
        AssertAlignedFor<T>(r.ptr);
        T* ptr = FoundationKitCxxStl::ConstructAt<T>(r.ptr, FoundationKitCxxStl::Forward<Args>(args)...);
        return ptr;
    }

    /// @brief Construct an object, returning an Expected for detailed error reporting.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]]
    Expected<T*, MemoryError> TryNew(Alloc& alloc, Args&&... args) noexcept {
        constexpr usize sz = SafeSizeOf<T>;
        constexpr usize al = SafeAlignOf<T>;
        AllocationResult r = alloc.Allocate(sz, al);
        if (!r) return MemoryError::OutOfMemory;
        AssertAllocResultValid(r, sz, al);
        AssertAlignedFor<T>(r.ptr);
        T* ptr = FoundationKitCxxStl::ConstructAt<T>(r.ptr, FoundationKitCxxStl::Forward<Args>(args)...);
        return ptr;
    }

    /// @brief Destroy an object and deallocate memory.
    template <typename T, IAllocator Alloc>
    void Delete(Alloc& alloc, T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        alloc.Deallocate(ptr, sizeof(T));
    }

    /// @brief Construct an array of objects, returning an Optional pointer.
    template <typename T, IAllocator Alloc>
    [[nodiscard]]
    Optional<T*> NewArray(Alloc& alloc, const usize count) noexcept {
        if (count == 0) return NullOpt;
        // Overflow-safe: CalculateArrayAllocationSize returns 0 on overflow.
        const usize total_size = CalculateArrayAllocationSize<T>(count);
        FK_BUG_ON(total_size == 0,
            "NewArray: size overflow computing {} elements of size {}", count, SafeSizeOf<T>);

        AllocationResult r = alloc.Allocate(total_size, SafeAlignOf<T>);
        if (!r) return NullOpt;
        AssertAllocResultValid(r, total_size, SafeAlignOf<T>);

        T* arr = static_cast<T*>(r.ptr);
        for (usize i = 0; i < count; ++i)
            FoundationKitCxxStl::ConstructAt<T>(arr + i);
        return arr;
    }

    /// @brief Destroy an array of objects and deallocate memory.
    template <typename T, IAllocator Alloc>
    void DeleteArray(Alloc& alloc, T* ptr, const usize count) noexcept {
        if (!ptr) return;
        for (usize i = count; i-- > 0;)
            ptr[i].~T();
        alloc.Deallocate(ptr, sizeof(T) * count);
    }

} // namespace FoundationKitMemory
