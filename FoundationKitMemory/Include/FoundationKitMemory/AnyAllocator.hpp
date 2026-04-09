#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/GlobalAllocator.hpp>

namespace FoundationKitMemory {

    /// @brief A type-erased allocator that can hold any IMemoryResource.
    /// @desc Delegates all operations to the contained resource.
    /// @warning Thread-safety depends on the underlying resource. If the resource is NOT thread-safe
    ///          (e.g., most single-threaded allocators), wrap with:
    ///          SynchronizedAllocator<AnyAllocator, Mutex>
    class AnyAllocator {
    public:
        /// @brief Default constructor stores nullptr.
        /// @desc  Callers that want the global allocator must call AnyAllocator::FromGlobal()
        ///        explicitly. This prevents silent null-pointer storage during early boot
        ///        when the global allocator may not yet be initialised.
        constexpr AnyAllocator() noexcept : m_resource(nullptr) {}

        explicit constexpr AnyAllocator(nullptr_t) noexcept : m_resource(nullptr) {}
        explicit constexpr AnyAllocator(BasicMemoryResource* resource) noexcept : m_resource(resource) {}

        /// @brief Explicitly adopt the system-wide global allocator.
        /// @desc  Crashes immediately with a clear boot-order message if the global
        ///        allocator has not yet been initialised — far easier to diagnose
        ///        than a deferred null-pointer crash at first use.
        [[nodiscard]] static AnyAllocator FromGlobal() noexcept {
            FK_BUG_ON(!IsGlobalAllocatorInitialized(),
                "AnyAllocator::FromGlobal: called before GlobalAllocatorSystem::Initialize(). "
                "Fix boot order: initialise the global allocator before any "
                "subsystem that calls AnyAllocator::FromGlobal().");
            return AnyAllocator(&GetGlobalAllocator());
        }

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
