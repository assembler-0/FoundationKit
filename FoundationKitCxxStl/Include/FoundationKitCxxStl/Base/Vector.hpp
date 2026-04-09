#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>

namespace FoundationKitCxxStl {

    /// @brief A dynamic array that can grow in size.
    /// @tparam T The type of elements.
    /// @tparam Alloc The allocator to use.
    template <typename T, FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class Vector {
        using _check = TypeSanityCheck<T>;
    public:
        using SizeType = usize;
        using Iterator = T*;
        using ConstIterator = const T*;

        explicit Vector(Alloc allocator = FoundationKitMemory::AnyAllocator::FromGlobal())
            : m_allocator(FoundationKitCxxStl::Move(allocator)), m_data(nullptr), m_size(0), m_capacity(0) {}

        ~Vector() {
            Clear();
            if (m_data) {
                m_allocator.Deallocate(m_data, m_capacity * sizeof(T));
            }
        }

        Vector(const Vector&) = delete;
        Vector& operator=(const Vector&) = delete;

        Vector(Vector&& other) noexcept
            : m_allocator(FoundationKitCxxStl::Move(other.m_allocator)), 
              m_data(other.m_data), 
              m_size(other.m_size), 
              m_capacity(other.m_capacity) {
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        Vector& operator=(Vector&& other) noexcept {
            if (this != &other) {
                Clear();
                if (m_data) m_allocator.Deallocate(m_data, m_capacity * sizeof(T));
                
                m_allocator = FoundationKitCxxStl::Move(other.m_allocator);
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                
                other.m_data = nullptr;
                other.m_size = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        template <typename... Args>
        bool PushBack(Args&&... args) {
            if (m_size == m_capacity) {
                if (!Reserve(m_capacity == 0 ? 8 : m_capacity * 2)) return false;
            }
            FK_BUG_ON(!m_data, "Vector: data pointer is null during PushBack");
            FoundationKitCxxStl::ConstructAt<T>(&m_data[m_size], FoundationKitCxxStl::Forward<Args>(args)...);
            m_size++;
            return true;
        }

        void PopBack() {
            FK_BUG_ON(m_size == 0, "Vector: PopBack() called on empty vector");
            m_size--;
            m_data[m_size].~T();
        }

        bool Reserve(SizeType new_capacity) {
            if (new_capacity <= m_capacity) return true;
            // Overflow check: new_capacity * sizeof(T) must not wrap.
            FK_BUG_ON(new_capacity > static_cast<SizeType>(-1) / sizeof(T),
                "Vector::Reserve: requested capacity ({}) would overflow size_t", new_capacity);

            auto res = m_allocator.Allocate(new_capacity * sizeof(T), alignof(T));
            if (!res.ok()) return false;

            T* new_data = static_cast<T*>(res.ptr);
            FK_BUG_ON(!new_data, "Vector: allocator returned success but null pointer");

            for (SizeType i = 0; i < m_size; ++i) {
                FoundationKitCxxStl::ConstructAt<T>(&new_data[i], FoundationKitCxxStl::Move(m_data[i]));
                m_data[i].~T();
            }

            if (m_data) {
                m_allocator.Deallocate(m_data, m_capacity * sizeof(T));
            }

            m_data = new_data;
            m_capacity = new_capacity;
            return true;
        }

        bool Resize(SizeType new_size) {
            if (new_size < m_size) {
                for (SizeType i = new_size; i < m_size; ++i) {
                    m_data[i].~T();
                }
                m_size = new_size;
                return true;
            }

            if (new_size > m_capacity) {
                if (!Reserve(new_size)) return false;
            }

            FK_BUG_ON(new_size > 0 && !m_data, "Vector: data pointer is null during Resize");

            for (SizeType i = m_size; i < new_size; ++i) {
                FoundationKitCxxStl::ConstructAt<T>(&m_data[i]);
            }
            m_size = new_size;
            return true;
        }

        void Clear() {
            for (SizeType i = 0; i < m_size; ++i) {
                m_data[i].~T();
            }
            m_size = 0;
        }

        [[nodiscard]] T& operator[](SizeType index) { 
            FK_BUG_ON(index >= m_size, "Vector: index ({}) out of bounds ({})", index, m_size);
            FK_BUG_ON(!m_data, "Vector: data pointer is null during access");
            return m_data[index]; 
        }

        [[nodiscard]] const T& operator[](SizeType index) const { 
            FK_BUG_ON(index >= m_size, "Vector: index ({}) out of bounds ({})", index, m_size);
            FK_BUG_ON(!m_data, "Vector: data pointer is null during access");
            return m_data[index]; 
        }

        [[nodiscard]] T& Front() { 
            FK_BUG_ON(m_size == 0, "Vector: Front() called on empty vector");
            FK_BUG_ON(!m_data, "Vector: data pointer is null during Front() access");
            return m_data[0]; 
        }

        [[nodiscard]] const T& Front() const { 
            FK_BUG_ON(m_size == 0, "Vector: Front() called on empty vector");
            FK_BUG_ON(!m_data, "Vector: data pointer is null during Front() access");
            return m_data[0]; 
        }

        [[nodiscard]] T& Back() { 
            FK_BUG_ON(m_size == 0, "Vector: Back() called on empty vector");
            FK_BUG_ON(!m_data, "Vector: data pointer is null during Back() access");
            return m_data[m_size - 1]; 
        }

        [[nodiscard]] const T& Back() const { 
            FK_BUG_ON(m_size == 0, "Vector: Back() called on empty vector");
            FK_BUG_ON(!m_data, "Vector: data pointer is null during Back() access");
            return m_data[m_size - 1]; 
        }

        [[nodiscard]] SizeType Size() const { return m_size; }
        [[nodiscard]] SizeType Capacity() const { return m_capacity; }
        [[nodiscard]] bool Empty() const { return m_size == 0; }

        Iterator begin() { return m_data; }
        Iterator end() { return m_data + m_size; }
        ConstIterator begin() const { return m_data; }
        ConstIterator end() const { return m_data + m_size; }

    private:
        Alloc    m_allocator;
        T*       m_data;
        SizeType m_size;
        SizeType m_capacity;
    };

} // namespace FoundationKitCxxStl
