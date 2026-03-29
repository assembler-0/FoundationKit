#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Meta/Concepts.hpp>
#include <FoundationKit/Base/Utility.hpp>

namespace FoundationKit::Memory {

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
    
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]]
    T* New(Alloc& alloc, Args&&... args) noexcept {
        AllocResult r = alloc.Allocate(sizeof(T), alignof(T));
        if (!r) return nullptr;
        return ::new (r.ptr) T(FoundationKit::Forward<Args>(args)...);
    }

    template <typename T, IAllocator Alloc>
    void Delete(Alloc& alloc, T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        alloc.Deallocate(ptr, sizeof(T));
    }

    template <typename T, IAllocator Alloc>
    [[nodiscard]]
    T* NewArray(Alloc& alloc, const usize count) noexcept {
        AllocResult r = alloc.Allocate(sizeof(T) * count, alignof(T));
        if (!r) return nullptr;
        T* arr = static_cast<T*>(r.ptr);
        for (usize i = 0; i < count; ++i)
            ::new (arr + i) T{};
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
