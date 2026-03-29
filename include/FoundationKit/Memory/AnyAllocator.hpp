#pragma once

#include <FoundationKit/Memory/Allocator.hpp>

namespace FoundationKit::Memory {

    /// @brief An abstract interface for allocators. 
    /// This allows passing allocators around without knowing their concrete type.
    class IAllocatorResource {
    public:
        virtual ~IAllocatorResource() = default;

        [[nodiscard]] virtual AllocResult Allocate(usize size, usize align) noexcept = 0;
        virtual void Deallocate(void* ptr, usize size) noexcept = 0;
        virtual bool Owns(void* ptr) const noexcept = 0;
    };

    /// @brief Wraps a concrete IAllocator into a polymorphic IAllocatorResource.
    /// This usually lives as a static or member variable in a specific context.
    template <IAllocator A>
    class AllocatorResource final : public IAllocatorResource {
    public:
        explicit constexpr AllocatorResource(A& alloc) noexcept : m_alloc(alloc) {}

        [[nodiscard]] AllocResult Allocate(usize size, usize align) noexcept override {
            return m_alloc.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept override {
            m_alloc.Deallocate(ptr, size);
        }

        bool Owns(void* ptr) const noexcept override {
            return m_alloc.Owns(ptr);
        }

    private:
        A& m_alloc;
    };

    /// @brief A lightweight, non-owning reference to any allocator resource.
    /// This is the unified way to pass allocators around in FoundationKit.
    class AnyAllocator {
    public:
        constexpr AnyAllocator() noexcept = default;
        constexpr AnyAllocator(nullptr_t) noexcept {}

        /// @brief Construct from a pointer to an existing resource.
        explicit constexpr AnyAllocator(IAllocatorResource* res) noexcept : m_resource(res) {}

        /// @brief Helper to wrap a concrete allocator using a temporary resource.
        /// WARNING: The resource must outlive the AnyAllocator. 
        /// Use this for global/static allocators or within a controlled scope.
        template <IAllocator A>
        static AnyAllocator From(AllocatorResource<A>& resource) noexcept {
            return AnyAllocator(&resource);
        }

        [[nodiscard]] AllocResult Allocate(const usize size, const usize align) const noexcept {
            return m_resource ? m_resource->Allocate(size, align) : AllocResult::failure();
        }

        void Deallocate(void* ptr, usize size) const noexcept {
            if (m_resource) m_resource->Deallocate(ptr, size);
        }

        [[nodiscard]] bool Owns(void* ptr) const noexcept {
            return m_resource ? m_resource->Owns(ptr) : false;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_resource != nullptr; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return IsValid(); }

    private:
        IAllocatorResource* m_resource = nullptr;
    };

    static_assert(IAllocator<AnyAllocator>, "FoundationKit: AnyAllocator must satisfy IAllocator concept.");

} // namespace FoundationKit::Memory
