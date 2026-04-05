#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

namespace FoundationKitCxxAbi {

// ----------------------------------------------------------------------------
// Feature flags
// ----------------------------------------------------------------------------

/// @brief Enable thread-safe local static guard protocol.
/// When OFF, guard acquire/release become no-ops (single-threaded kernels).
#ifndef FOUNDATIONKITCXXABI_THREAD_SAFE_STATICS
#  define FOUNDATIONKITCXXABI_THREAD_SAFE_STATICS 1
#endif

/// @brief Enable the built-in Itanium ABI demangler.
/// When OFF, __cxa_demangle writes "<demangling disabled>" and returns -1.
#ifndef FOUNDATIONKITCXXABI_ENABLE_DEMANGLE
#  define FOUNDATIONKITCXXABI_ENABLE_DEMANGLE 1
#endif

/// @brief Enable the __cxa_atexit / __cxa_finalize registry.
#ifndef FOUNDATIONKITCXXABI_ENABLE_ATEXIT
#  define FOUNDATIONKITCXXABI_ENABLE_ATEXIT 1
#endif

/// @brief Maximum number of __cxa_atexit registrations.
/// Each slot is 3 pointers (24 bytes on 64-bit). 256 entries = 6 KiB static.
#ifndef FOUNDATIONKITCXXABI_ATEXIT_MAX
#  define FOUNDATIONKITCXXABI_ATEXIT_MAX 256
#endif

/// @brief When set, guard acquire spins calling OslThreadYield instead of
///        pure CPU pause. Only safe after the kernel scheduler is live.
#ifndef FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD
#  define FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD 0
#endif

/// @brief When set, operator new/delete forward to FoundationKitMemory's
///        GlobalAllocator. Adds a link-time dependency on FoundationKitMemory.
#ifndef FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
#  define FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR 0
#endif

// ----------------------------------------------------------------------------
// Guard object word layout (Itanium ABI, §3.3.2)
//
// On 64-bit targets the guard variable is 8 bytes. Byte 0 is the
// *initialized* sentinel; the remaining bytes are implementation-defined.
// We use the full 64-bit word with the following bit assignment:
//
//   bit 0  — INITIALIZED  (1 = initializer has completed successfully)
//   bit 1  — IN_PROGRESS  (1 = some thread is currently inside the initializer)
//   bit 2..63 — reserved (waiter count / future use)
//
// On 32-bit targets (not currently supported), layout would be 4 bytes.
// ----------------------------------------------------------------------------
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64) || defined(FOUNDATIONKITPLATFORM_ARCH_ARM64) || defined(FOUNDATIONKITPLATFORM_ARCH_RISCV64)
#  define FOUNDATIONKITCXXABI_GUARD_INITIALIZED_BIT  (1ULL << 0)
#  define FOUNDATIONKITCXXABI_GUARD_IN_PROGRESS_BIT  (1ULL << 1)
#endif

} // namespace FoundationKitCxxAbi
