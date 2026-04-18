#pragma once

// ============================================================================
// FoundationKitCxxAbi — Global Constructor / Destructor Walker
// ============================================================================
// Linker script contract:
//   The linker must export:
//     __init_array_start, __init_array_end  — bounds of .init_array section
//     __fini_array_start, __fini_array_end  — bounds of .fini_array section
//   Example (GNU ld):
//     PROVIDE_HIDDEN(__init_array_start = .);
//     KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
//     PROVIDE_HIDDEN(__init_array_end   = .);
// ============================================================================

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitCxxAbi::Init {
    using namespace FoundationKitCxxStl;

    /// @brief One registration in the __cxa_atexit table.
    struct AtExitEntry {
        void (*destructor)(void *); ///< The registered destructor function.
        void *obj_ptr; ///< Argument passed to destructor.
        void *dso_handle; ///< DSO tag for selective finalization.
    };

    /// @brief Walk .init_array and call every global constructor in order.
    /// @note  Must be called exactly once, before any C++ globals are used.
    ///        Calling it a second time will re-run constructors — DO NOT do that.
    void RunGlobalConstructors() noexcept;

    /// @brief Call all registered __cxa_atexit destructors (LIFO order).
    /// @note  Equivalent to __cxa_finalize(nullptr).
    ///        Must be called exactly once during orderly kernel shutdown.
    void RunGlobalDestructors() noexcept;

    /// @brief Walk .fini_array and call every registered fini function.
    /// @note  Called automatically by RunGlobalDestructors(). Public in case
    ///        the kernel wants to run fini_array explicitly before atexit handlers.
    void RunFiniArray() noexcept;

    /// @brief Query how many atexit slots are currently used.
    /// @return Number of registered destructors.
    [[nodiscard]] usize AtExitUsed() noexcept;

    /// @brief Query the maximum atexit registry capacity.
    /// @return FOUNDATIONKITCXXABI_ATEXIT_MAX.
    [[nodiscard]] consteval usize AtExitCapacity() noexcept {
        return FOUNDATIONKITCXXABI_ATEXIT_MAX;
    }
} // namespace FoundationKitCxxAbi::Init
