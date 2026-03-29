#pragma once

#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Base/Utility.hpp>

namespace FoundationKit::Memory {

    /// @brief Smart pointer that owns a resource and deallocates it using a specific allocator.
    template <typename T, IAllocator Alloc>
    class UniquePtr {
    public:
        constexpr UniquePtr() noexcept = default;

        explicit constexpr UniquePtr(nullptr_t) noexcept {}

        UniquePtr(T* ptr, Alloc& alloc) noexcept 
            : m_ptr(ptr), m_alloc(&alloc) {}

        ~UniquePtr() noexcept {
            Reset();
        }

        // Non-copyable
        UniquePtr(const UniquePtr&) = delete;
        UniquePtr& operator=(const UniquePtr&) = delete;

        // Move-only
        UniquePtr(UniquePtr&& other) noexcept
            : m_ptr(other.m_ptr), m_alloc(other.m_alloc) {
            other.m_ptr = nullptr;
        }

        UniquePtr& operator=(UniquePtr&& other) noexcept {
            if (this != &other) {
                Reset();
                m_ptr = other.m_ptr;
                m_alloc = other.m_alloc;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        void Reset(T* ptr = nullptr) noexcept {
            if (m_ptr && m_alloc) {
                Delete(*m_alloc, m_ptr);
            }
            m_ptr = ptr;
        }

        [[nodiscard]] T* Release() noexcept {
            T* ptr = m_ptr;
            m_ptr = nullptr;
            return ptr;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator*()  const noexcept { return *m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

    private:
        T* m_ptr = nullptr;
        Alloc* m_alloc = nullptr;
    };

    /// @brief Helper to create a UniquePtr using a specific allocator.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]] UniquePtr<T, Alloc> MakeUnique(Alloc& alloc, Args&&... args) noexcept {
        T* ptr = New<T>(alloc, FoundationKit::Forward<Args>(args)...);
        return UniquePtr<T, Alloc>(ptr, alloc);
    }

} // namespace FoundationKit::Memory
