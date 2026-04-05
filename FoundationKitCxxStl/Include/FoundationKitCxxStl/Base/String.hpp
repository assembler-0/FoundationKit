#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Vector.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/UniquePtr.hpp>

namespace FoundationKitCxxStl {

    /// @brief A dynamic string class with Small String Optimization (SSO).
    /// @tparam Alloc The allocator for heap-based storage.
    template <FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class String {
    public:
        using SizeType = usize;
        static constexpr SizeType SsoCapacity = 23;

        String() noexcept 
            : m_allocator(), m_size(0), m_is_heap(false) {
            m_data.sso[0] = '\0';
        }

        explicit String(Alloc allocator) noexcept 
            : m_allocator(FoundationKitCxxStl::Move(allocator)), m_size(0), m_is_heap(false) {
            m_data.sso[0] = '\0';
        }

        ~String() noexcept {
            if (m_is_heap) {
                m_data.heap.~UniquePtr();
            }
        }

        String(const String&) = delete;
        String& operator=(const String&) = delete;

        String(String&& other) noexcept 
            : m_allocator(FoundationKitCxxStl::Move(other.m_allocator)), 
              m_size(other.m_size), 
              m_heap_capacity(other.m_heap_capacity),
              m_is_heap(other.m_is_heap) {
            if (m_is_heap) {
                FoundationKitCxxStl::ConstructAt<FoundationKitMemory::UniquePtr<char[], Alloc>>(&m_data.heap, FoundationKitCxxStl::Move(other.m_data.heap));
            } else {
                FoundationKitMemory::MemoryCopy(m_data.sso, other.m_data.sso, SsoCapacity + 1);
            }
            other.m_size = 0;
            other.m_is_heap = false;
            other.m_data.sso[0] = '\0';
        }

        String& operator=(String&& other) noexcept {
            if (this != &other) {
                if (m_is_heap) {
                    m_data.heap.~UniquePtr();
                }
                
                m_allocator = FoundationKitCxxStl::Move(other.m_allocator);
                m_size = other.m_size;
                m_is_heap = other.m_is_heap;
                m_heap_capacity = other.m_heap_capacity;
                
                if (m_is_heap) {
                    FoundationKitCxxStl::ConstructAt<FoundationKitMemory::UniquePtr<char[], Alloc>>(&m_data.heap, FoundationKitCxxStl::Move(other.m_data.heap));
                } else {
                    FoundationKitMemory::MemoryCopy(m_data.sso, other.m_data.sso, SsoCapacity + 1);
                }
                
                other.m_size = 0;
                other.m_is_heap = false;
                other.m_data.sso[0] = '\0';
            }
            return *this;
        }

        Expected<void, FoundationKitMemory::MemoryError> Append(const StringView view) noexcept {
            const SizeType old_size = m_size;
            const SizeType new_size = old_size + view.Size();

            if (auto res = EnsureCapacity(new_size); !res) return res;

            char* buffer = m_is_heap ? m_data.heap.Get() : m_data.sso;
            FoundationKitMemory::MemoryCopy(buffer + old_size, view.Data(), view.Size());
            
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

        [[nodiscard]] char operator[](SizeType index) const noexcept {
            return CStr()[index];
        }

        [[nodiscard]] char At(SizeType index) const noexcept {
            FK_BUG_ON(index >= m_size, "String: At index ({}) out of bounds ({})", index, m_size);
            return CStr()[index];
        }

        [[nodiscard]] char Front() const noexcept {
            FK_BUG_ON(m_size == 0, "String: Front() called on empty string");
            return CStr()[0];
        }

        [[nodiscard]] char Back() const noexcept {
            FK_BUG_ON(m_size == 0, "String: Back() called on empty string");
            return CStr()[m_size - 1];
        }

        [[nodiscard]] const char* CStr() const noexcept {
            return m_is_heap ? m_data.heap.Get() : m_data.sso;
        }

        [[nodiscard]] explicit operator StringView() const noexcept {
            return {CStr(), m_size};
        }

        [[nodiscard]] bool StartsWith(const StringView view) const noexcept {
            if (view.Size() > m_size) return false;
            return FoundationKitMemory::MemoryCompare(CStr(), view.Data(), view.Size()) == 0;
        }

        [[nodiscard]] bool EndsWith(const StringView view) const noexcept {
            if (view.Size() > m_size) return false;
            return FoundationKitMemory::MemoryCompare(CStr() + (m_size - view.Size()), view.Data(), view.Size()) == 0;
        }

        [[nodiscard]] bool Contains(const StringView view) const noexcept {
            return static_cast<StringView>(*this).Contains(view);
        }

        [[nodiscard]] usize Find(const StringView view, const usize offset = 0) const noexcept {
            return static_cast<StringView>(*this).Find(view, offset);
        }

        [[nodiscard]] usize RFind(char c, usize offset = StringView::NPos) const noexcept {
            return static_cast<StringView>(*this).RFind(c, offset);
        }

        [[nodiscard]] Expected<String, FoundationKitMemory::MemoryError> SubStr(usize offset, usize count = static_cast<usize>(-1)) const noexcept {
            if (offset > m_size) return Unexpected(FoundationKitMemory::MemoryError::InvalidSize);
            
            const usize actual_count = count == static_cast<usize>(-1) || offset + count > m_size
                                       ? m_size - offset 
                                       : count;
            
            String result(m_allocator);
            if (auto res = result.Append(StringView(CStr() + offset, actual_count)); !res) return Unexpected(res.Error());
            return result;
        }

        [[nodiscard]] Expected<Vector<String<Alloc>>, FoundationKitMemory::MemoryError> Split(char delimiter) const noexcept {
            Vector<String<Alloc>> result(m_allocator);
            auto view = static_cast<StringView>(*this);
            usize start = 0;
            
            for (usize i = 0; i < view.Size(); ++i) {
                if (view[i] == delimiter) {
                    auto sub = SubStr(start, i - start);
                    if (!sub) return Unexpected(sub.Error());
                    if (!result.PushBack(FoundationKitCxxStl::Move(sub.Value()))) return Unexpected(FoundationKitMemory::MemoryError::OutOfMemory);
                    start = i + 1;
                }
            }

            auto sub = SubStr(start);
            if (!sub) return Unexpected(sub.Error());
            if (!result.PushBack(FoundationKitCxxStl::Move(sub.Value()))) return Unexpected(FoundationKitMemory::MemoryError::OutOfMemory);

            return result;
        }

        void Trim(StringView chars = " \t\n\r") noexcept {
            if (m_size == 0) return;

            auto view = static_cast<StringView>(*this);
            StringView trimmed = view.Trim(chars);

            if (trimmed.Empty()) {
                Clear();
                return;
            }

            if (trimmed.Data() != view.Data()) {
                FoundationKitMemory::MemoryMove(m_is_heap ? m_data.heap.Get() : m_data.sso, trimmed.Data(), trimmed.Size());
            }
            
            m_size = trimmed.Size();
            char* write_buffer = m_is_heap ? m_data.heap.Get() : m_data.sso;
            write_buffer[m_size] = '\0';
        }

    private:
        Expected<void, FoundationKitMemory::MemoryError> EnsureCapacity(const SizeType required) noexcept {
            if (required <= SsoCapacity && !m_is_heap) return {};

            const SizeType current_cap = m_is_heap ? m_heap_capacity : SsoCapacity;
            if (required <= current_cap) return {};

            SizeType next_cap = current_cap * 2;
            if (next_cap < required) next_cap = required;

            const FoundationKitMemory::AllocResult res = m_allocator.Allocate(next_cap + 1, alignof(char));
            if (!res.ok()) return Unexpected(FoundationKitMemory::MemoryError::OutOfMemory);

            auto new_ptr = static_cast<char*>(res.ptr);
            const char* old_ptr = CStr();
            
            FoundationKitMemory::MemoryCopy(new_ptr, old_ptr, m_size);
            new_ptr[m_size] = '\0';
            
            if (m_is_heap) {
                m_data.heap.~UniquePtr();
            }

            m_is_heap = true;
            FoundationKitCxxStl::ConstructAt<FoundationKitMemory::UniquePtr<char[], Alloc>>(&m_data.heap, new_ptr, next_cap + 1, m_allocator);
            m_heap_capacity = next_cap;
            
            return {};
        }

        Alloc                m_allocator;
        SizeType             m_size;
        SizeType             m_heap_capacity = 0;
        bool                 m_is_heap;

        union Data {
            char  sso[SsoCapacity + 1];
            FoundationKitMemory::UniquePtr<char[], Alloc> heap;

            constexpr Data() : sso{} {}
            ~Data() {} 
        } m_data;
    };

} // namespace FoundationKitCxxStl
