#pragma once

#include <FoundationKit/Memory/Allocator.hpp>

namespace FoundationKit::Memory {
    /// @brief Pure virtual interface for memory providers.
    struct MemoryResource {
        virtual ~MemoryResource() = default;
        virtual AllocResult Allocate(usize size, usize align) noexcept = 0;
        virtual void Deallocate(void* ptr, usize size) noexcept = 0;
        virtual bool Owns(void* ptr) const noexcept = 0;
    };

    /// @brief Concrete wrapper for any IAllocator.
    template <IAllocator Alloc>
    struct ConcreteResource final : MemoryResource {
        Alloc& m_alloc;
        explicit constexpr ConcreteResource(Alloc& alloc) noexcept : m_alloc(alloc) {}

        AllocResult Allocate(usize size, usize align) noexcept override { return m_alloc.Allocate(size, align); }
        void Deallocate(void* ptr, usize size) noexcept override { m_alloc.Deallocate(ptr, size); }
        bool Owns(void* ptr) const noexcept override { return m_alloc.Owns(ptr); }
    };

    /// @brief A type-erased allocator value-type.
    class AnyAllocator {
    public:
        /// @brief Static registry for the system-wide default resource.
        static void SetDefaultResource(MemoryResource* res) noexcept {
            GetDefaultResourceInternal() = res;
        }

        static MemoryResource* GetDefaultResource() noexcept {
            return GetDefaultResourceInternal();
        }

        /// @brief Default constructor: Adopts the system-wide default resource.
        AnyAllocator() noexcept : m_resource(GetDefaultResource()) {}

        /// @brief Explicitly construct with a specific resource or nullptr.
        explicit constexpr AnyAllocator(nullptr_t) noexcept {}
        explicit constexpr AnyAllocator(MemoryResource* res) noexcept : m_resource(res) {}

        [[nodiscard]] AllocResult Allocate(const usize size, const usize align) const noexcept {
            FK_BUG_ON(!m_resource, "AnyAllocator: attempting to allocate from null resource");
            return m_resource->Allocate(size, align);
        }

        void Deallocate(void* ptr, const usize size) const noexcept {
            if (m_resource) {
                m_resource->Deallocate(ptr, size);
            } else {
                FK_BUG_ON(ptr != nullptr, "AnyAllocator: attempting to deallocate non-null pointer from null resource");
            }
        }

        [[nodiscard]] bool Owns(void* ptr) const noexcept {
            return m_resource ? m_resource->Owns(ptr) : false;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_resource != nullptr; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] MemoryResource* GetResource() const noexcept { return m_resource; }

    private:
        static MemoryResource*& GetDefaultResourceInternal() noexcept {
            static MemoryResource* s_default = nullptr;
            return s_default;
        }

        MemoryResource* m_resource = nullptr;
    };

    static_assert(IAllocator<AnyAllocator>);

} // namespace FoundationKit::Memory
