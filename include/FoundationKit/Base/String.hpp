#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/Expected.hpp>
#include <FoundationKit/Base/StringView.hpp>
#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Memory/AnyAllocator.hpp>
#include <FoundationKit/Memory/Operations.hpp>
#include <FoundationKit/Memory/UniquePtr.hpp>

namespace FoundationKit {

    /// @brief A dynamic string class with Small String Optimization (SSO).
    /// @tparam Alloc The allocator for heap-based storage.
    template <Memory::IAllocator Alloc = Memory::AnyAllocator>
    class String {
    public:
        using SizeType = usize;
        static constexpr SizeType SsoCapacity = 23;

        String() noexcept 
            : m_allocator(), m_size(0), m_is_heap(false) {
            m_data.sso[0] = '\0';
        }

        explicit String(Alloc allocator) noexcept 
            : m_allocator(FoundationKit::Move(allocator)), m_size(0), m_is_heap(false) {
            m_data.sso[0] = '\0';
        }

        ~String() noexcept {
            if (m_is_heap) {
                if (char* ptr = m_data.heap.Release()) {
                    m_allocator.Deallocate(static_cast<void*>(ptr), m_heap_capacity + 1);
                }
            }
        }

        String(const String&) = delete;
        String& operator=(const String&) = delete;

        String(String&& other) noexcept 
            : m_allocator(FoundationKit::Move(other.m_allocator)), 
              m_size(other.m_size), 
              m_heap_capacity(other.m_heap_capacity),
              m_is_heap(other.m_is_heap) {
            if (m_is_heap) {
                m_data.heap = FoundationKit::Move(other.m_data.heap);
            } else {
                Memory::MemoryCopy(m_data.sso, other.m_data.sso, SsoCapacity + 1);
            }
            other.m_size = 0;
            other.m_is_heap = false;
            other.m_data.sso[0] = '\0';
        }

        String& operator=(String&& other) noexcept {
            if (this != &other) {
                if (m_is_heap) {
                    if (char* ptr = m_data.heap.Release()) m_allocator.Deallocate(static_cast<void*>(ptr), m_heap_capacity + 1);
                }
                
                m_allocator = FoundationKit::Move(other.m_allocator);
                m_size = other.m_size;
                m_is_heap = other.m_is_heap;
                m_heap_capacity = other.m_heap_capacity;
                
                if (m_is_heap) {
                    m_data.heap = FoundationKit::Move(other.m_data.heap);
                } else {
                    Memory::MemoryCopy(m_data.sso, other.m_data.sso, SsoCapacity + 1);
                }
                
                other.m_size = 0;
                other.m_is_heap = false;
                other.m_data.sso[0] = '\0';
            }
            return *this;
        }

        Expected<void, Memory::MemoryError> Append(const StringView view) noexcept {
            const SizeType old_size = m_size;
            const SizeType new_size = old_size + view.Size();

            if (auto res = EnsureCapacity(new_size); !res) return res;

            char* buffer = m_is_heap ? m_data.heap.Get() : m_data.sso;
            Memory::MemoryCopy(buffer + old_size, view.Data(), view.Size());
            
            m_size = new_size;
            buffer[m_size] = '\0';
            
            return {};
        }

        void Clear() noexcept {
            m_size = 0;
            if (m_is_heap) m_data.heap.Get()[0] = '\0';
            else m_data.sso[0] = '\0';
        }

        [[nodiscard]] constexpr SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0; }

        [[nodiscard]] const char* CStr() const noexcept {
            return m_is_heap ? m_data.heap.Get() : m_data.sso;
        }

        [[nodiscard]] explicit operator StringView() const noexcept {
            return {CStr(), m_size};
        }

    private:
        Expected<void, Memory::MemoryError> EnsureCapacity(const SizeType required) noexcept {
            if (required <= SsoCapacity && !m_is_heap) return {};

            const SizeType current_cap = m_is_heap ? m_heap_capacity : SsoCapacity;
            if (required <= current_cap) return {};

            SizeType next_cap = current_cap * 2;
            if (next_cap < required) next_cap = required;

            const Memory::AllocResult res = m_allocator.Allocate(next_cap + 1, alignof(char));
            if (!res.ok()) return Memory::MemoryError::OutOfMemory;

            auto new_ptr = static_cast<char*>(res.ptr);
            const char* old_ptr = CStr();
            
            Memory::MemoryCopy(new_ptr, old_ptr, m_size);
            new_ptr[m_size] = '\0';
            
            if (m_is_heap) {
                if (char* ptr = m_data.heap.Release()) m_allocator.Deallocate(static_cast<void*>(ptr), m_heap_capacity + 1);
            }

            m_is_heap = true;
            FoundationKit::ConstructAt<Memory::UniquePtr<char[]>>(&m_data.heap, new_ptr, next_cap + 1, Memory::AnyAllocator(m_allocator));
            m_heap_capacity = next_cap;
            
            return {};
        }

        Alloc                m_allocator;
        SizeType             m_size;
        SizeType             m_heap_capacity = 0;
        bool                 m_is_heap;

        union Data {
            char  sso[SsoCapacity + 1];
            Memory::UniquePtr<char[]> heap;

            constexpr Data() : sso{} {}
            ~Data() {} 
        } m_data;
    };

} // namespace FoundationKit
