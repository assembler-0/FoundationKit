#pragma once

#include <FoundationKitMemory/MemoryRegion.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Global Allocator Registry: Named Allocator Bindings
    // ============================================================================

    /// @brief Centralized registry for named allocators (not pointer-based dispatch).
    /// @desc Supports multi-image and kernel scenarios:
    ///       - Named lookups (safer than global pointers)
    ///       - Manual lifetime management (no static local initialization)
    ///       - Thread-safe acquisition via external synchronization
    ///       - Statistics and debugging hooks
    ///
    /// NOTE: Access to this registry is NOT internally synchronized. The caller
    /// must ensure thread-safety by:
    /// 1. Wrapping allocators with SynchronizedAllocator before accessing from multiple threads
    /// 2. OR ensuring single-threaded access during early boot, then freezing registry
    /// 3. OR using higher-level synchronization (mutex, rwlock) around registry calls
    class GlobalAllocatorRegistry {
    public:
        static constexpr usize MaxNameLength = 32;
        static constexpr usize MaxRegisteredAllocators = 16;

        /// @brief Named allocator binding.
        struct AllocatorBinding {
            char name[MaxNameLength];
            BasicMemoryResource* allocator = nullptr;
            MemoryRegion region;
            bool is_default = false;
        };

        /// @brief Initialize the registry with a default allocator.
        /// @param default_alloc The fallback allocator for unnamed requests
        /// @param default_region The region associated with the default allocator
        constexpr GlobalAllocatorRegistry(
            BasicMemoryResource* default_alloc,
            MemoryRegion default_region) noexcept
            : m_default_allocator(default_alloc), m_default_region(default_region),
              m_allocator_count(0) {
            FK_BUG_ON(default_alloc == nullptr, 
                "GlobalAllocatorRegistry: default allocator cannot be null");
            FK_BUG_ON(!default_region.IsValid(), 
                "GlobalAllocatorRegistry: default region must be valid");
        }

        /// @brief Register a named allocator.
        /// @param name Null-terminated name (max MaxNameLength bytes)
        /// @param alloc The allocator to register
        /// @param region The region this allocator manages
        /// @return true if registration succeeded, false if registry is full or name taken
        bool Register(const char* name, BasicMemoryResource* alloc, MemoryRegion region) noexcept {
            if (alloc == nullptr || name == nullptr) {
                return false;
            }

            // Check for name collision
            for (usize i = 0; i < m_allocator_count; ++i) {
                if (StringsEqual(m_bindings[i].name, name)) {
                    return false;  // Name already in use
                }
            }

            if (m_allocator_count >= MaxRegisteredAllocators) {
                return false;  // Registry full
            }

            // Copy name (bounded)
            CopyStringBounded(m_bindings[m_allocator_count].name, name, MaxNameLength);
            m_bindings[m_allocator_count].allocator = alloc;
            m_bindings[m_allocator_count].region = region;
            m_bindings[m_allocator_count].is_default = false;

            m_allocator_count++;
            return true;
        }

        /// @brief Unregister a named allocator.
        /// @param name The allocator to remove
        /// @return true if unregistered, false if not found
        bool Unregister(const char* name) noexcept {
            for (usize i = 0; i < m_allocator_count; ++i) {
                if (StringsEqual(m_bindings[i].name, name)) {
                    // Swap with last entry and shrink
                    if (i < m_allocator_count - 1) {
                        m_bindings[i] = m_bindings[m_allocator_count - 1];
                    }
                    m_allocator_count--;
                    return true;
                }
            }
            return false;
        }

        /// @brief Query a named allocator.
        /// @param name The allocator name to find
        /// @return Pointer to allocator, or nullptr if not found
        [[nodiscard]] BasicMemoryResource* Query(const char* name) const noexcept {
            for (usize i = 0; i < m_allocator_count; ++i) {
                if (StringsEqual(m_bindings[i].name, name)) {
                    return m_bindings[i].allocator;
                }
            }
            return nullptr;
        }

        /// @brief Get the region of a named allocator.
        /// @param name The allocator name
        /// @return The region, or invalid region if not found
        [[nodiscard]] MemoryRegion QueryRegion(const char* name) const noexcept {
            for (usize i = 0; i < m_allocator_count; ++i) {
                if (StringsEqual(m_bindings[i].name, name)) {
                    return m_bindings[i].region;
                }
            }
            return MemoryRegion();  // Invalid region
        }

        /// @brief Get the default allocator.
        [[nodiscard]] BasicMemoryResource* GetDefault() noexcept {
            return m_default_allocator;
        }

        /// @brief Get the default region.
        [[nodiscard]] MemoryRegion GetDefaultRegion() const noexcept {
            return m_default_region;
        }

        /// @brief Set a new default allocator.
        /// @param alloc The new default allocator
        /// @param region The associated region
        void SetDefault(BasicMemoryResource* alloc, MemoryRegion region) noexcept {
            FK_BUG_ON(alloc == nullptr, 
                "GlobalAllocatorRegistry::SetDefault: allocator cannot be null");
            m_default_allocator = alloc;
            m_default_region = region;
        }

        /// @brief Get number of registered allocators.
        [[nodiscard]] constexpr usize Count() const noexcept {
            return m_allocator_count;
        }

        /// @brief Get a binding by index.
        /// @param idx Index (0 <= idx < Count())
        [[nodiscard]] constexpr const AllocatorBinding* At(usize idx) const noexcept {
            FK_BUG_ON(idx >= m_allocator_count, 
                "GlobalAllocatorRegistry::At: index out of bounds");
            return &m_bindings[idx];
        }

        /// @brief Clear all allocators except default.
        void Clear() noexcept {
            m_allocator_count = 0;
        }

    private:
        /// @brief Compare two null-terminated strings.
        [[nodiscard]] static constexpr bool StringsEqual(const char* a, const char* b) noexcept {
            while (*a && *b) {
                if (*a != *b) return false;
                a++;
                b++;
            }
            return *a == *b;  // Both null-terminated
        }

        /// @brief Copy string with bounds.
        static constexpr void CopyStringBounded(char* dst, const char* src, usize max) noexcept {
            usize i = 0;
            while (i < max - 1 && *src) {
                dst[i] = *src;
                src++;
                i++;
            }
            dst[i] = '\0';
        }

        BasicMemoryResource* m_default_allocator;
        MemoryRegion m_default_region;
        AllocatorBinding m_bindings[MaxRegisteredAllocators];
        usize m_allocator_count;
    };

    // ============================================================================
    // Global Registry Instance (extern, not static local)
    // ============================================================================

    /// @brief Get the global allocator registry (must be initialized externally).
    /// @desc This function returns a pointer to the global registry.
    ///       The registry itself must be initialized via InitializeGlobalRegistry().
    [[nodiscard]] GlobalAllocatorRegistry* GetGlobalAllocatorRegistry() noexcept;

    /// @brief Initialize the global registry with a default allocator.
    /// @param default_alloc The fallback allocator
    /// @param default_region The region for the default allocator
    /// @desc Must be called exactly once at startup.
    void InitializeGlobalAllocatorRegistry(
        BasicMemoryResource* default_alloc,
        MemoryRegion default_region) noexcept;

    /// @brief Shutdown the global registry (releases resources if needed).
    void ShutdownGlobalAllocatorRegistry() noexcept;

} // namespace FoundationKitMemory
