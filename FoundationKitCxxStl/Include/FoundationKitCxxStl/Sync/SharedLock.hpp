#pragma once

#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl::Sync {

    /// ============================================================================
    /// SharedLock: RAII wrapper for shared ownership of a SharedLockable.
    /// ============================================================================
    template <SharedLockable LockType>
    class SharedLock {
    public:
        SharedLock() noexcept : m_lock(nullptr), m_owned(false) {}

        explicit SharedLock(LockType& lock) noexcept : m_lock(&lock), m_owned(false) {
            Lock();
        }

        ~SharedLock() noexcept {
            if (m_owned) Unlock();
        }

        // Non-copyable
        SharedLock(const SharedLock&) = delete;
        SharedLock& operator=(const SharedLock&) = delete;

        // Movable
        SharedLock(SharedLock&& other) noexcept 
            : m_lock(other.m_lock), m_owned(other.m_owned) {
            other.m_lock  = nullptr;
            other.m_owned = false;
        }

        SharedLock& operator=(SharedLock&& other) noexcept {
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
            if (m_lock && !m_owned) {
                m_lock->LockShared();
                m_owned = true;
            }
        }

        bool TryLock() noexcept {
            if (m_lock && !m_owned) {
                m_owned = m_lock->TryLockShared();
            }
            return m_owned;
        }

        void Unlock() noexcept {
            if (m_lock && m_owned) {
                m_lock->UnlockShared();
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
