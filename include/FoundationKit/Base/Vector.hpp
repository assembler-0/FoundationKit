#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/Optional.hpp>
#include <FoundationKit/Base/Expected.hpp>
#include <FoundationKit/Meta/Concepts.hpp>
#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Memory/AnyAllocator.hpp>
#include <FoundationKit/Memory/Operations.hpp>
#include <FoundationKit/Memory/UniquePtr.hpp>

namespace FoundationKit {

    /// @brief A dynamic array container that uses an allocator for memory management.
    /// @tparam T The type of elements.
    /// @tparam Alloc The allocator type, must satisfy IAllocator.
    template <typename T, Memory::IAllocator Alloc = Memory::AnyAllocator>
    class Vector {
    public:
        using ValueType      = T;
        using SizeType       = usize;
        using Reference      = T&;
        using ConstReference = const T&;
        using Pointer        = T*;
        using ConstPointer   = const T*;
        using Iterator       = T*;
        using ConstIterator  = const T*;

        /// @brief Construct a vector with a specific allocator.
        explicit Vector(Alloc allocator) noexcept 
            : m_allocator(FoundationKit::Move(allocator)), m_size(0), m_capacity(0) {}

        /// @brief Default constructor using the default AnyAllocator.
        Vector() noexcept 
            : m_allocator(), m_size(0), m_capacity(0) {}

        ~Vector() noexcept {
            Clear();
        }

        Vector(const Vector&) = delete;
        Vector& operator=(const Vector&) = delete;

        Vector(Vector&& other) noexcept 
            : m_allocator(FoundationKit::Move(other.m_allocator)), 
              m_storage(FoundationKit::Move(other.m_storage)), 
              m_size(other.m_size), 
              m_capacity(other.m_capacity) {
            other.m_size = 0;
            other.m_capacity = 0;
        }

        Vector& operator=(Vector&& other) noexcept {
            if (this != &other) {
                Clear();
                m_allocator = FoundationKit::Move(other.m_allocator);
                m_storage = FoundationKit::Move(other.m_storage);
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.m_size = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        Expected<void, Memory::MemoryError> PushBack(const T& value) noexcept {
            if (auto res = EnsureCapacity(m_size + 1); !res) return res;
            
            FoundationKit::ConstructAt<T>(Data() + m_size, value);
            m_size++;
            return {};
        }

        template <typename... Args>
        Expected<void, Memory::MemoryError> EmplaceBack(Args&&... args) noexcept {
            if (auto res = EnsureCapacity(m_size + 1); !res) return res;

            FoundationKit::ConstructAt<T>(Data() + m_size, FoundationKit::Forward<Args>(args)...);
            m_size++;
            return {};
        }

        void PopBack() noexcept {
            if (m_size > 0) {
                m_size--;
                Data()[m_size].~T();
            }
        }

        void Clear() noexcept {
            for (usize i = 0; i < m_size; ++i) {
                Data()[i].~T();
            }
            m_size = 0;
        }

        Expected<void, Memory::MemoryError> Reserve(const SizeType new_capacity) noexcept {
            if (new_capacity <= m_capacity) return {};

            const usize bytes = new_capacity * sizeof(T);
            Memory::AllocResult result = m_allocator.Allocate(bytes, alignof(T));
            if (!result.ok()) return Expected<void, Memory::MemoryError>(Memory::MemoryError::OutOfMemory);

            T* new_ptr = static_cast<T*>(result.ptr);
            T* old_ptr = Data();
            
            if constexpr (TriviallyCopyable<T>) {
                Memory::MemoryCopy(new_ptr, old_ptr, m_size * sizeof(T));
            } else {
                for (usize i = 0; i < m_size; ++i) {
                    FoundationKit::ConstructAt<T>(new_ptr + i, FoundationKit::Move(old_ptr[i]));
                    old_ptr[i].~T();
                }
            }

            m_storage = Memory::UniquePtr<u8[]>(reinterpret_cast<u8*>(new_ptr), bytes, Memory::AnyAllocator(m_allocator));
            m_capacity = new_capacity;
            
            return {};
        }

        [[nodiscard]] Optional<Reference> At(SizeType index) noexcept {
            if (index >= m_size) return NullOpt;
            return Data()[index];
        }

        [[nodiscard]] Optional<ConstReference> At(SizeType index) const noexcept {
            if (index >= m_size) return NullOpt;
            return Data()[index];
        }

        [[nodiscard]] Reference operator[](SizeType index) noexcept { return Data()[index]; }
        [[nodiscard]] ConstReference operator[](SizeType index) const noexcept { return Data()[index]; }

        [[nodiscard]] Reference Front() noexcept { return Data()[0]; }
        [[nodiscard]] ConstReference Front() const noexcept { return Data()[0]; }

        [[nodiscard]] Reference Back() noexcept { return Data()[m_size - 1]; }
        [[nodiscard]] ConstReference Back() const noexcept { return Data()[m_size - 1]; }

        [[nodiscard]] Pointer Data() noexcept { return reinterpret_cast<T*>(m_storage.Get()); }
        [[nodiscard]] ConstPointer Data() const noexcept { return reinterpret_cast<const T*>(m_storage.Get()); }

        [[nodiscard]] Iterator Begin() noexcept { return Data(); }
        [[nodiscard]] ConstIterator Begin() const noexcept { return Data(); }

        [[nodiscard]] Iterator End() noexcept { return Data() + m_size; }
        [[nodiscard]] ConstIterator End() const noexcept { return Data() + m_size; }

        [[nodiscard]] Iterator begin() noexcept { return Begin(); }
        [[nodiscard]] ConstIterator begin() const noexcept { return Begin(); }
        [[nodiscard]] Iterator end() noexcept { return End(); }
        [[nodiscard]] ConstIterator end() const noexcept { return End(); }

        [[nodiscard]] SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] SizeType Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] bool Empty() const noexcept { return m_size == 0; }

    private:
        Expected<void, Memory::MemoryError> EnsureCapacity(const SizeType required) noexcept {
            if (required <= m_capacity) return {};
            SizeType next_capacity = m_capacity == 0 ? 8 : m_capacity * 2;
            while (next_capacity < required) next_capacity *= 2;
            return Reserve(next_capacity);
        }

        Alloc        m_allocator;
        Memory::UniquePtr<u8[]> m_storage;
        usize        m_size;
        usize        m_capacity;
    };

} // namespace FoundationKit
