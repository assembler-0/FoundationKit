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
    template <IAllocator A>
    class AllocatorResource final : public IAllocatorResource {
    public:
        explicit AllocatorResource(A& alloc) noexcept : m_alloc(alloc) {}

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

    /// @brief A lightweight, non-owning pointer to any allocator resource.
    /// Used by generic data structures that don't want to be templated on the allocator type.
    class AnyAllocator {
    public:
        constexpr AnyAllocator() noexcept = default;

        explicit constexpr AnyAllocator(nullptr_t) noexcept {}

        /// @brief Construct from a pointer to an existing resource.
        explicit constexpr AnyAllocator(IAllocatorResource* res) noexcept : m_resource(res) {}

        /// @brief Helper to wrap a concrete allocator (caller must ensure lifetime).
        template <IAllocator A>
        static AnyAllocator From(A& alloc) noexcept {
            static AllocatorResource<A> resource(alloc);
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

        [[nodiscard]] explicit operator bool() const noexcept { return m_resource != nullptr; }

    private:
        IAllocatorResource* m_resource = nullptr;
    };

} // namespace FoundationKit::Memory
