#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // ============================================================================
    // Global Allocator System
    // ============================================================================

    /// @brief Manages the process-wide global allocator.
    /// @desc This system ensures:
    ///       - A single, mandatory allocator for the entire process
    ///       - Thread-safe initialization (one-time only)
    ///       - Deterministic failures if used before initialization
    ///       - Clear ownership and lifecycle semantics
    /// @warning The global allocator MUST be initialized before any memory allocation.
    ///          Failure to do so will trigger FK_BUG_ON() at allocation time.
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
                // File-scope storage: avoids the function-local static __cxa_guard_acquire
                // that a function-local static would emit. The CAS below already prevents
                // double-initialisation, so the guard is redundant and dangerous at early boot.
                alignas(AllocatorWrapper<Alloc>)
                static byte s_wrapper_storage[sizeof(AllocatorWrapper<Alloc>)];

                if (m_allocator.Load(MemoryOrder::Acquire) == nullptr) {
                    resource_ptr = FoundationKitCxxStl::ConstructAt<AllocatorWrapper<Alloc>>(
                        s_wrapper_storage, allocator
                    );
                } else {
                    resource_ptr = reinterpret_cast<AllocatorWrapper<Alloc>*>(s_wrapper_storage);
                }
            }

            FK_BUG_ON(resource_ptr == nullptr, "GlobalAllocatorSystem::Initialize: failed to resolve memory resource pointer");

            BasicMemoryResource* expected = nullptr;
            const bool won = m_allocator.CompareExchange(
                expected, resource_ptr,
                /*weak=*/false,
                MemoryOrder::SeqCst,
                MemoryOrder::SeqCst
            );

            if (!won) {
                FK_LOG_WARN("GlobalAllocatorSystem initialized more than once - ignoring subsequent initialization");
            }
        }

        /// @brief Retrieve the global allocator.
        /// @return Reference to the initialized global allocator
        /// @throws Via FK_BUG_ON() if not initialized
        /// @desc This function ALWAYS returns a valid allocator or bugs.
        ///       There is no null case—the global allocator is mandatory.
        [[nodiscard]] static BasicMemoryResource& GetAllocator() noexcept {
            BasicMemoryResource* alloc = m_allocator.Load(MemoryOrder::SeqCst);
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
            return m_allocator.Load(MemoryOrder::SeqCst) != nullptr;
        }

    private:
        /// @brief Atomic storage for the global allocator pointer.
        static Atomic<BasicMemoryResource*> m_allocator;

        // Prevent construction of this class (static interface only)
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
