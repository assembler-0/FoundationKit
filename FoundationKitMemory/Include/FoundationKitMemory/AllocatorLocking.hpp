#pragma once

/// @file AllocatorLocking.hpp
/// @brief Unified SMP-safe locking policy framework for FoundationKitMemory allocators.
///
/// # Design Overview
/// This module provides a concept-based framework for composing allocators with
/// different synchronization strategies. All locks are provided by FoundationKitCxxStl::Sync,
/// ensuring consistency and freestanding compliance.
///
/// # Lock Options (from FoundationKitCxxStl::Sync)
/// - **NullLock**: Zero-cost single-threaded (static methods, no state)
/// - **SpinLock**: Atomic-based busy-wait, SMP-safe, no OSL dependency
/// - **Mutex**: OSL-backed sleep/wake with initial spin phase (blocking)
/// - **SharedSpinLock**: Multi-reader, single-writer spin-based
/// - **InterruptSafeLock<T>**: Wraps any BasicLockable with interrupt disabling
/// - **TicketLock**: Fair FIFO ordering for SMP environments
///
/// # Usage Pattern
/// Wrap any IAllocator with SynchronizedAllocator to enable thread safety:
///
/// ```cpp
/// // Single-threaded (default, zero overhead)
/// PoolAllocator<256> pool;
///
/// // Thread-safe with spinlock (busy-wait, good for short critical sections)
/// SynchronizedAllocator<PoolAllocator<256>, SpinLock> spin_pool(pool);
///
/// // Thread-safe with mutex (sleeps on contention, good for high load)
/// Mutex pool_mutex;
/// PoolAllocator<256> base_pool;
/// SynchronizedAllocator<PoolAllocator<256>, Mutex> sleeping_pool(base_pool);
///
/// // Multi-reader, single-writer (for allocators with read-heavy access patterns)
/// SynchronizedAllocator<PoolAllocator<256>, SharedSpinLock> shared_pool(pool);
///
/// // Interrupt-safe spinlock (disables interrupts during critical section)
/// SpinLock spinlock;
/// InterruptSafeLock<SpinLock> irq_safe(spinlock);
/// SynchronizedAllocator<PoolAllocator<256>, InterruptSafeLock<SpinLock>> irq_pool(pool);
/// ```
///
/// # Safety Guarantees
/// - **SMP-Safe**: All locks use atomic operations with proper memory barriers
/// - **Freestanding**: No exceptions, RTTI, or libc dependencies
/// - **Zero-Cost Abstraction**: NullLock compiles to empty code
/// - **RAII Safety**: LockGuard/UniqueLock ensure automatic release
///
/// # Memory Ordering
/// All locks follow standard acquire/release semantics:
/// - Lock() establishes Acquire barrier: subsequent memory operations cannot move before
/// - Unlock() establishes Release barrier: prior memory operations cannot move after
/// - Protects: concurrent access to allocator state (free lists, metadata)

#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>
#include <FoundationKitCxxStl/Sync/TicketLock.hpp>
#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // AllocatorLockPolicy Concept
    // ============================================================================

    /// @brief Concept: A synchronization mechanism suitable for protecting allocators.
    /// @desc Requires Lock(), Unlock(), and TryLock() methods (the Lockable concept).
    ///       Any type satisfying Sync::Lockable can be used as an allocator lock policy.
    ///       Examples: NullLock, SpinLock, Mutex, SharedSpinLock, TicketLock, etc.
    /// @tparam LockPolicy The type to check for allocator lock compatibility
    template <typename LockPolicy>
    concept AllocatorLockPolicy = Lockable<LockPolicy>;

    // ============================================================================
    // Allocator Lock Selection Type Trait
    // ============================================================================

    /// @brief Type trait: Default lock policy for an allocator type.
    /// @desc Users and libraries can specialize this trait to set a different
    ///       default lock for their allocator implementations.
    /// @tparam AllocatorType The allocator to select a default lock for
    /// @example
    ///   // Make a custom allocator thread-safe by default
    ///   template <>
    ///   struct DefaultAllocatorLock<ThreadSafeBuddyAllocator> {
    ///       using Type = Sync::SpinLock;
    ///   };
    template <typename AllocatorType>
    struct DefaultAllocatorLock {
        using Type = Sync::NullLock;  // Single-threaded by default (zero overhead)
    };

    /// @brief Helper alias: Get the default lock type for an allocator.
    /// @tparam AllocatorType The allocator type to get the default lock for
    template <typename AllocatorType>
    using DefaultAllocatorLockType = DefaultAllocatorLock<AllocatorType>::Type;

    // ============================================================================
    // Lock Policy Selection Helper (for advanced use cases)
    // ============================================================================

    /// @brief Selects the appropriate lock policy based on context.
    /// @desc Useful for library code that wants to choose locks dynamically based
    ///       on configuration or platform features.
    /// @tparam SingleThreaded If true, returns NullLock (zero overhead)
    /// @tparam AllowSleep If true and SingleThreaded is false, uses Mutex; else SpinLock
    template <bool SingleThreaded, bool AllowSleep = true>
    struct SelectAllocatorLock;

    // Single-threaded specialization
    template <>
    struct SelectAllocatorLock<true, true> {
        using Type = Sync::NullLock;
    };

    template <>
    struct SelectAllocatorLock<true, false> {
        using Type = Sync::NullLock;
    };

    // Multi-threaded with sleep
    template <>
    struct SelectAllocatorLock<false, true> {
        using Type = Sync::Mutex;
    };

    // Multi-threaded without sleep (Interrupt-safe TicketLock)
    template <>
    struct SelectAllocatorLock<false, false> {
        using Type = Sync::InterruptSafeTicketLock;
    };

    /// @brief Helper alias for SelectAllocatorLock.
    template <bool SingleThreaded, bool AllowSleep = true>
    using SelectAllocatorLockType = typename SelectAllocatorLock<SingleThreaded, AllowSleep>::Type;

} // namespace FoundationKitMemory
