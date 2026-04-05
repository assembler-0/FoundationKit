#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>

namespace FoundationKitMemory {

    /// @brief Smart pointer that owns a resource and deallocates it using a specific allocator.
    template <typename T, IAllocator Alloc = AnyAllocator>
    class UniquePtr {
    public:
        using Pointer = T*;
        using ElementType = T;

        constexpr UniquePtr() noexcept = default;
        explicit constexpr UniquePtr(nullptr_t) noexcept {}

        constexpr UniquePtr(T* ptr, const Alloc& alloc) noexcept 
            : m_ptr(ptr), m_alloc(alloc) {}

        ~UniquePtr() noexcept {
            Reset();
        }

        UniquePtr(const UniquePtr&) = delete;
        UniquePtr& operator=(const UniquePtr&) = delete;

        constexpr UniquePtr(UniquePtr&& other) noexcept
            : m_ptr(other.m_ptr), m_alloc(FoundationKitCxxStl::Move(other.m_alloc)) {
            other.m_ptr = nullptr;
        }

        UniquePtr& operator=(UniquePtr&& other) noexcept {
            if (this != &other) {
                Reset();
                m_ptr = other.m_ptr;
                m_alloc = FoundationKitCxxStl::Move(other.m_alloc);
                other.m_ptr = nullptr;
            }
            return *this;
        }

        void Reset(T* ptr = nullptr) noexcept {
            if (m_ptr) {
                Delete(m_alloc, m_ptr);
            }
            m_ptr = ptr;
        }

        [[nodiscard]] T* Release() noexcept {
            T* ptr = m_ptr;
            m_ptr = nullptr;
            return ptr;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        
        [[nodiscard]] T& operator*() const noexcept { 
            FK_BUG_ON(!m_ptr, "UniquePtr: dereferencing null pointer");
            return *m_ptr; 
        }
        
        [[nodiscard]] T* operator->() const noexcept { 
            FK_BUG_ON(!m_ptr, "UniquePtr: access via null pointer");
            return m_ptr; 
        }
        
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        [[nodiscard]] const Alloc& GetAllocator() const noexcept { return m_alloc; }

    private:
        T* m_ptr = nullptr;
        Alloc m_alloc{};
    };

    /// @brief Specialization for arrays.
    template <typename T, IAllocator Alloc>
    class UniquePtr<T[], Alloc> {
    public:
        using Pointer = T*;
        using ElementType = T;

        constexpr UniquePtr() noexcept = default;
        explicit constexpr UniquePtr(nullptr_t) noexcept {}

        constexpr UniquePtr(T* ptr, const usize count, const Alloc& alloc) noexcept
            : m_ptr(ptr), m_count(count), m_alloc(alloc) {}

        ~UniquePtr() noexcept {
            Reset();
        }

        UniquePtr(const UniquePtr&) = delete;
        UniquePtr& operator=(const UniquePtr&) = delete;

        constexpr UniquePtr(UniquePtr&& other) noexcept
            : m_ptr(other.m_ptr), m_count(other.m_count), m_alloc(FoundationKitCxxStl::Move(other.m_alloc)) {
            other.m_ptr = nullptr;
            other.m_count = 0;
        }

        UniquePtr& operator=(UniquePtr&& other) noexcept {
            if (this != &other) {
                Reset();
                m_ptr = other.m_ptr;
                m_count = other.m_count;
                m_alloc = FoundationKitCxxStl::Move(other.m_alloc);
                other.m_ptr = nullptr;
                other.m_count = 0;
            }
            return *this;
        }

        void Reset(T* ptr = nullptr, const usize count = 0) noexcept {
            if (m_ptr) {
                DeleteArray(m_alloc, m_ptr, m_count);
            }
            m_ptr = ptr;
            m_count = count;
        }

        [[nodiscard]] T* Release() noexcept {
            T* ptr = m_ptr;
            m_ptr = nullptr;
            m_count = 0;
            return ptr;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        
        [[nodiscard]] T& operator[](usize index) const noexcept { 
            FK_BUG_ON(!m_ptr, "UniquePtr[]: access via null pointer");
            FK_BUG_ON(index >= m_count, "UniquePtr[]: index ({}) out of bounds ({})", index, m_count);
            return m_ptr[index]; 
        }
        
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }
        [[nodiscard]] usize Size() const noexcept { return m_count; }

    private:
        T*    m_ptr   = nullptr;
        usize m_count = 0;
        Alloc m_alloc{};
    };

    /// @brief Factory methods for UniquePtr.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]] UniquePtr<T, Alloc> MakeUnique(Alloc alloc, Args&&... args) noexcept {
        auto res = New<T>(alloc, FoundationKitCxxStl::Forward<Args>(args)...);
        if (!res) return UniquePtr<T, Alloc>();
        return UniquePtr<T, Alloc>(res.Value(), alloc);
    }

    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]] Expected<UniquePtr<T, Alloc>, MemoryError> TryMakeUnique(Alloc alloc, Args&&... args) noexcept {
        auto res = TryNew<T>(alloc, FoundationKitCxxStl::Forward<Args>(args)...);
        if (!res) return res.Error();
        return UniquePtr<T, Alloc>(res.Value(), alloc);
    }

    /// @brief Factory for array UniquePtr.
    template <typename T, IAllocator Alloc>
    [[nodiscard]] UniquePtr<T[], Alloc> MakeUniqueArray(Alloc alloc, usize count) noexcept {
        auto res = NewArray<T>(alloc, count);
        if (!res) return UniquePtr<T[], Alloc>();
        return UniquePtr<T[], Alloc>(res.Value(), count, alloc);
    }

} // namespace FoundationKitMemory

// ============================================================================
// Formatter Specializations
// ============================================================================

namespace FoundationKitCxxStl {

    template <typename T, FoundationKitMemory::IAllocator Alloc>
    struct Formatter<FoundationKitMemory::UniquePtr<T, Alloc>> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitMemory::UniquePtr<T, Alloc>& value, const FormatSpec& spec = {}) const noexcept {
            sb.Append("UniquePtr(");
            Formatter<T*>().Format(sb, value.Get(), spec);
            sb.Append(')');
        }
    };

    template <typename T, FoundationKitMemory::IAllocator Alloc>
    struct Formatter<FoundationKitMemory::UniquePtr<T[], Alloc>> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitMemory::UniquePtr<T[], Alloc>& value, const FormatSpec& spec = {}) const noexcept {
            sb.Append("UniquePtr[](");
            Formatter<T*>().Format(sb, value.Get(), spec);
            sb.Append(')');
        }
    };

} // namespace FoundationKitCxxStl
