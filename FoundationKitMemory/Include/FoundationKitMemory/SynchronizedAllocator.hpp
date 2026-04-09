#pragma once

#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/SharedLock.hpp>
#include <FoundationKitMemory/MemoryCore.hpp>

namespace FoundationKitMemory {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Sync;

// ============================================================================
// SynchronizedAllocator: Concurrency Decorator
// ============================================================================

/// @brief Wraps any IAllocator with a locking policy for SMP safety.
/// @desc Ensures that memory operations are thread-safe.
///       Works even if the OSL scheduler is not yet active (using SpinLock).
/// @tparam BaseAllocator Must satisfy IAllocator<BaseAllocator>
/// @tparam LockType Locking policy (default
/// FoundationKitCxxStl::Sync::InterruptSafeTicketLock)
template <IAllocator BaseAllocator, typename LockType = InterruptSafeTicketLock>
class SynchronizedAllocator {
public:
  // ====================================================================
  // Construction
  // ====================================================================

  explicit constexpr SynchronizedAllocator(BaseAllocator &base) noexcept
      : m_base(base) {}

  // Non-copyable
  SynchronizedAllocator(const SynchronizedAllocator &) = delete;
  SynchronizedAllocator &operator=(const SynchronizedAllocator &) = delete;

  // ====================================================================
  // IAllocator Implementation
  // ====================================================================

  [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
    LockGuard lock(m_lock);
    return m_base.Allocate(size, align);
  }

  void Deallocate(void *ptr, usize size) noexcept {
    LockGuard lock(m_lock);
    m_base.Deallocate(ptr, size);
  }

  void Deallocate(void *ptr) noexcept {
    LockGuard lock(m_lock);
    if constexpr (requires { m_base.Deallocate(ptr); }) {
      m_base.Deallocate(ptr);
    } else {
      m_base.Deallocate(ptr, 0);
    }
  }

  [[nodiscard]] bool Owns(const void *ptr) const noexcept {
    if constexpr (SharedLockable<LockType>) {
      SharedLock lock(m_lock);
      return m_base.Owns(ptr);
    } else {
      LockGuard lock(m_lock);
      return m_base.Owns(ptr);
    }
  }

  // ====================================================================
  // Extended Capabilities
  // ====================================================================

  [[nodiscard]] AllocationResult
  Reallocate(void *ptr, usize old_size, usize new_size, usize align) noexcept {
    LockGuard lock(m_lock);
    if constexpr (IReallocatableAllocator<BaseAllocator>) {
      return m_base.Reallocate(ptr, old_size, new_size, align);
    } else {
      // Manual fallback inside the lock
      if (new_size == 0) {
        m_base.Deallocate(ptr, old_size);
        return AllocationResult::Failure();
      }
      if (new_size <= old_size)
        return {ptr, new_size};

      const AllocationResult res = m_base.Allocate(new_size, align);
      if (!res)
        return res;

      if (ptr) {
        MemoryCopy(res.ptr, ptr, old_size);
        m_base.Deallocate(ptr, old_size);
      }
      return res;
    }
  }

  void DeallocateAll() noexcept {
    LockGuard lock(m_lock);
    if constexpr (IClearableAllocator<BaseAllocator>) {
      m_base.DeallocateAll();
    }
  }

private:
  BaseAllocator &m_base;
  mutable LockType m_lock;
};

} // namespace FoundationKitMemory
