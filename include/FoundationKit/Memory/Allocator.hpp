#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Meta/Concepts.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/Optional.hpp>
#include <FoundationKit/Base/Expected.hpp>

namespace FoundationKit::Memory {

    /// @brief Possible memory errors for rich error reporting.
    enum class MemoryError : u8 {
        None = 0,
        OutOfMemory,
        InvalidAlignment,
        InvalidSize,
        NotOwned
    };

    /// @brief Result of an allocation attempt.
    struct AllocResult {
        void* ptr;
        usize size;

        [[nodiscard]] constexpr bool ok() const noexcept { return ptr != nullptr; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }

        static constexpr AllocResult failure() noexcept { return {nullptr, 0}; }
    };

    /// @brief Core Allocator Concept (PnP Infrastructure)
    template <typename A>
    concept IAllocator = requires(A& alloc, void* ptr, usize size, usize align) {
        /// @brief Allocate memory of 'size' with 'align' alignment.
        { alloc.Allocate(size, align) } -> SameAs<AllocResult>;

        /// @brief Deallocate memory previously allocated.
        { alloc.Deallocate(ptr, size) } -> SameAs<void>;

        /// @brief Check if this allocator owns the given pointer.
        { alloc.Owns(ptr) } -> SameAs<bool>;
    };

    /// @brief Extended Allocator Concept for those that can reallocate.
    template <typename A>
    concept IReallocatable = IAllocator<A> && requires(A& alloc, void* ptr, usize old_size, usize new_size, usize align) {
        { alloc.Reallocate(ptr, old_size, new_size, align) } -> SameAs<AllocResult>;
    };

    /// @brief Extended Allocator Concept for those that can clear all at once.
    template <typename A>
    concept IClearable = IAllocator<A> && requires(A& alloc) {
        { alloc.DeallocateAll() } -> SameAs<void>;
    };

    /// @brief Global helpers for object construction/destruction using an allocator.

    /// @brief Construct an object, returning an Optional pointer.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]]
    Optional<T*> New(Alloc& alloc, Args&&... args) noexcept {
        AllocResult r = alloc.Allocate(sizeof(T), alignof(T));
        if (!r) return NullOpt;
        
        T* ptr = FoundationKit::ConstructAt<T>(r.ptr, FoundationKit::Forward<Args>(args)...);
        return ptr;
    }

    /// @brief Construct an object, returning an Expected for detailed error reporting.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]]
    Expected<T*, MemoryError> TryNew(Alloc& alloc, Args&&... args) noexcept {
        AllocResult r = alloc.Allocate(sizeof(T), alignof(T));
        if (!r) return MemoryError::OutOfMemory;
        
        T* ptr = FoundationKit::ConstructAt<T>(r.ptr, FoundationKit::Forward<Args>(args)...);
        return ptr;
    }

    template <typename T, IAllocator Alloc>
    void Delete(Alloc& alloc, T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        alloc.Deallocate(ptr, sizeof(T));
    }

    template <typename T, IAllocator Alloc>
    [[nodiscard]]
    Optional<T*> NewArray(Alloc& alloc, const usize count) noexcept {
        AllocResult r = alloc.Allocate(sizeof(T) * count, alignof(T));
        if (!r) return NullOpt;
        
        T* arr = static_cast<T*>(r.ptr);
        for (usize i = 0; i < count; ++i)
            FoundationKit::ConstructAt<T>(arr + i);
        return arr;
    }

    template <typename T, IAllocator Alloc>
    void DeleteArray(Alloc& alloc, T* ptr, const usize count) noexcept {
        if (!ptr) return;
        for (usize i = count; i-- > 0;)
            ptr[i].~T();
        alloc.Deallocate(ptr, sizeof(T) * count);
    }

} // namespace FoundationKit::Memory
