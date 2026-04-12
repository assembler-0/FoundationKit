#pragma once

#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Sync/Rcu.hpp>
#include <FoundationKitCxxStl/Sync/RcuPtr.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // ============================================================================
    // Global Allocator System
    // ============================================================================

    /// @brief Manages the process-wide global allocator.
    ///
    /// ## RcuPtr integration
    ///
    /// The allocator pointer is written exactly once (at boot) and then read
    /// on every allocation. Previously it used a SeqCst atomic load on every
    /// GetAllocator() call — the most expensive memory order, causing a full
    /// memory barrier on every read.
    ///
    /// RcuPtr replaces this with an Acquire load (the minimum correct ordering
    /// for a published pointer) and defers reclamation through the domain.
    /// Since the global allocator is never replaced after boot, the reclaim
    /// callback is never registered in practice — the RcuPtr is used purely
    /// for its Acquire-load read path.
    class GlobalAllocatorSystem {
    public:
        /// @brief Initialize the global allocator.
        /// @param allocator Reference to an allocator implementing IAllocator
        /// @desc This function should be called exactly once during system bootstrap.
        ///       If called multiple times, it will emit a warning via OslLog() and ignore
        ///       subsequent calls (only the first initialization takes effect).
        /// @warning Calling this after bootstrap is complete may cause unpredictable behavior.
        ///          It is the caller's responsibility to ensure thread-safe initialization.
        template <IAllocator Alloc>
        static void Initialize(Alloc& allocator) noexcept {
            BasicMemoryResource* resource_ptr = nullptr;

            if constexpr (FoundationKitCxxStl::BaseOf<BasicMemoryResource, Alloc>) {
                resource_ptr = static_cast<BasicMemoryResource*>(&allocator);
            } else {
                alignas(AllocatorWrapper<Alloc>)
                static byte s_wrapper_storage[sizeof(AllocatorWrapper<Alloc>)];

                if (m_allocator.UnsafeGet() == nullptr) {
                    resource_ptr = FoundationKitCxxStl::ConstructAt<AllocatorWrapper<Alloc>>(
                        s_wrapper_storage, allocator
                    );
                } else {
                    resource_ptr = reinterpret_cast<AllocatorWrapper<Alloc>*>(s_wrapper_storage);
                }
            }

            FK_BUG_ON(resource_ptr == nullptr,
                "GlobalAllocatorSystem::Initialize: failed to resolve memory resource pointer");

            // StoreInitial uses a Release store — correct for a once-written pointer.
            // FK_BUG_ON fires if called twice (StoreInitial asserts the old value is null).
            if (m_allocator.UnsafeGet() != nullptr) {
                FK_LOG_WARN("GlobalAllocatorSystem initialized more than once - ignoring subsequent initialization");
                return;
            }
            m_allocator.StoreInitial(resource_ptr);
        }

        /// @brief Retrieve the global allocator.
        /// @return Reference to the initialized global allocator
        /// @throws Via FK_BUG_ON() if not initialized
        /// @desc This function ALWAYS returns a valid allocator or bugs.
        ///       There is no null case—the global allocator is mandatory.
        [[nodiscard]] static BasicMemoryResource& GetAllocator() noexcept {
            // Acquire load via RcuPtr — pairs with the Release store in StoreInitial.
            BasicMemoryResource* alloc = m_allocator.Load();
            FK_BUG_ON(alloc == nullptr,
                "GlobalAllocatorSystem::GetAllocator: FATAL - Global allocator accessed before initialization! "
                "The kernel must call InitializeGlobalAllocator() during early bootstrap.");
            return *alloc;
        }

        /// @brief Check if the global allocator is initialized.
        /// @return true if Initialize() has been called, false otherwise
        /// @desc Utility function for optional pre-checks. Most code should just call
        ///       GetAllocator() and let it bug if not initialized.
        [[nodiscard]] static bool IsInitialized() noexcept {
            return m_allocator.Load() != nullptr;
        }

    private:
        // RcuPtr with a 1-CPU domain: the global allocator is written once at boot.
        // The domain is never armed (no grace period needed) — RcuPtr is used
        // purely for its Acquire-load read path and Release-store publish path.
        using GlobalAllocDomain = FoundationKitCxxStl::Sync::RcuDomain<1>;
        static GlobalAllocDomain                                          m_domain;
        static FoundationKitCxxStl::Sync::RcuPtr<BasicMemoryResource,
                                                  GlobalAllocDomain>      m_allocator;

        GlobalAllocatorSystem() = delete;
        ~GlobalAllocatorSystem() = delete;
    };

    // ============================================================================
    // Bridge for operator new/delete
    // ============================================================================

    [[nodiscard]] inline void* GlobalAllocate(usize size) noexcept {
        auto res = GlobalAllocatorSystem::GetAllocator().Allocate(size, 16);
        return res.ptr;
    }

    inline void GlobalDeallocate(void* ptr) noexcept {
        GlobalAllocatorSystem::GetAllocator().Deallocate(ptr);
    }

    inline void GlobalDeallocate(void* ptr, usize size) noexcept {
        GlobalAllocatorSystem::GetAllocator().Deallocate(ptr, size);
    }

    // ============================================================================
    // Public API Aliases
    // ============================================================================

    /// @brief Initialize the global allocator with an explicit allocator instance.
    /// @param allocator Reference to any allocator implementing IAllocator
    /// @example
    ///     static BumpAllocator my_alloc(buffer, size);
    ///     InitializeGlobalAllocator(my_alloc);
    ///     // Now all allocation operations use my_alloc
    template <IAllocator Alloc>
    void InitializeGlobalAllocator(Alloc& allocator) noexcept {
        GlobalAllocatorSystem::Initialize(allocator);
    }

    /// @brief Retrieve the global allocator.
    /// @return Reference to the initialized global allocator
    /// @throws Via FK_BUG_ON() if Initialize() has not been called
    /// @example
    ///     auto result = GetGlobalAllocator().Allocate(size, align);
    [[nodiscard]] inline BasicMemoryResource& GetGlobalAllocator() noexcept {
        return GlobalAllocatorSystem::GetAllocator();
    }

    /// @brief Check if the global allocator is initialized.
    /// @return true if Initialize() has been called
    [[nodiscard]] inline bool IsGlobalAllocatorInitialized() noexcept {
        return GlobalAllocatorSystem::IsInitialized();
    }

} // namespace FoundationKitMemory
