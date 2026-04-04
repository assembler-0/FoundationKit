#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/Global.hpp>

namespace FoundationKitMemory {

    /// @brief A type-erased allocator that can hold any IMemoryResource.
    /// @desc Delegates all operations to the contained resource.
    /// @warning Thread-safety depends on the underlying resource. If the resource is NOT thread-safe
    ///          (e.g., most single-threaded allocators), wrap with:
    ///          SynchronizedAllocator<AnyAllocator, Mutex>
    class AnyAllocator {
    public:
        /// @brief Adopts the system-wide default resource by default.
        AnyAllocator() noexcept : m_resource(GetDefaultResource()) {}

        explicit constexpr AnyAllocator(nullptr_t) noexcept : m_resource(nullptr) {}
        explicit constexpr AnyAllocator(BasicMemoryResource* resource) noexcept : m_resource(resource) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) const noexcept {
            if (!m_resource) return AllocationResult::Failure();
            return m_resource->Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) const noexcept {
            if (m_resource) m_resource->Deallocate(ptr, size);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_resource ? m_resource->Owns(ptr) : false;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_resource != nullptr; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
        
        [[nodiscard]] BasicMemoryResource* GetResource() const noexcept { return m_resource; }

    private:
        BasicMemoryResource* m_resource = nullptr;
    };

    static_assert(IAllocator<AnyAllocator>);

} // namespace FoundationKitMemory
