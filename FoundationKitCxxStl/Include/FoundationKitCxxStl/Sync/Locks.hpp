#pragma once

#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

/// @brief sets the default lock
#define FOUNDATIONKIT_DEFAULT_LOCK ::FoundationKitCxxStl::Sync::SpinLock

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl::Base;

    // ========================================================================
    // Lock Concepts (for synchronization primitives)
    // ========================================================================

    /// @brief Basic lock interface: Lock() and Unlock().
    template <typename L>
    concept BasicLockable = requires(L& lock) {
        { lock.Lock() } -> SameAs<void>;
        { lock.Unlock() } -> SameAs<void>;
    };

    /// @brief Enhanced lockable: Lock(), Unlock(), TryLock().
    template <typename L>
    concept Lockable = BasicLockable<L> && requires(L& lock) {
        { lock.TryLock() } -> SameAs<bool>;
    };

    /// @brief Shared lock interface: LockShared(), UnlockShared(), Lock() for exclusive, Unlock() for unlock.
    template <typename L>
    concept SharedLockable = requires(L& lock) {
        { lock.LockShared() } -> SameAs<void>;
        { lock.UnlockShared() } -> SameAs<void>;
        { lock.Lock() } -> SameAs<void>;
        { lock.Unlock() } -> SameAs<void>;
    };

    /// ============================================================================
    /// NullLock: Zero-overhead policy for single-threaded/per-CPU scenarios.
    /// ============================================================================
    struct NullLock {
        static constexpr void Lock()    noexcept {}
        static constexpr void Unlock()  noexcept {}
        static constexpr bool TryLock() noexcept { return true; }
    };

    /// ============================================================================
    /// LockGuard: Simple RAII wrapper for BasicLockable types.
    /// ============================================================================
    template <BasicLockable LockType>
    class LockGuard {
    public:
        explicit LockGuard(LockType& lock) noexcept : m_lock(lock) {
            m_lock.Lock();
        }

        ~LockGuard() noexcept {
            m_lock.Unlock();
        }

        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;

    private:
        LockType& m_lock;
    };

    /// ============================================================================
    /// UniqueLock: Advanced RAII wrapper with manual control.
    /// ============================================================================
    template <Lockable LockType>
    class UniqueLock {
    public:
        UniqueLock() noexcept : m_lock(nullptr), m_owned(false) {}

        explicit UniqueLock(LockType& lock) noexcept : m_lock(&lock), m_owned(false) {
            Lock();
        }

        ~UniqueLock() noexcept {
            if (m_owned) Unlock();
        }

        // Non-copyable
        UniqueLock(const UniqueLock&) = delete;
        UniqueLock& operator=(const UniqueLock&) = delete;

        // Movable
        UniqueLock(UniqueLock&& other) noexcept 
            : m_lock(other.m_lock), m_owned(other.m_owned) {
            other.m_lock  = nullptr;
            other.m_owned = false;
        }

        UniqueLock& operator=(UniqueLock&& other) noexcept {
            if (this != &other) {
                if (m_owned) Unlock();
                m_lock  = other.m_lock;
                m_owned = other.m_owned;
                other.m_lock  = nullptr;
                other.m_owned = false;
            }
            return *this;
        }

        void Lock() noexcept {
            FK_BUG_ON(m_owned, "UniqueLock::Lock: already owns the lock (double-lock)");
            if (m_lock) {
                m_lock->Lock();
                m_owned = true;
            }
        }

        bool TryLock() noexcept {
            if (m_lock && !m_owned) {
                m_owned = m_lock->TryLock();
            }
            return m_owned;
        }

        void Unlock() noexcept {
            FK_BUG_ON(m_lock && !m_owned, "UniqueLock::Unlock: does not own the lock (unlock without lock)");
            if (m_lock && m_owned) {
                m_lock->Unlock();
                m_owned = false;
            }
        }

        [[nodiscard]] bool IsOwned() const noexcept { return m_owned; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_owned; }

    private:
        LockType* m_lock;
        bool      m_owned;
    };

} // namespace FoundationKitCxxStl::Sync
