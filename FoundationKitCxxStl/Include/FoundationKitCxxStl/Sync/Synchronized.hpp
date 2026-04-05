#pragma once

#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/SharedLock.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>

namespace FoundationKitCxxStl::Sync {

    /// @brief Provides thread-safe access to an object of type T.
    /// @desc Wraps an object and a lock, ensuring all access is synchronized.
    /// @tparam T The type to wrap.
    /// @tparam LockType The lock type to use (default FOUNDATIONKIT_DEFAULT_LOCK).
    template <typename T, typename LockType = FOUNDATIONKIT_DEFAULT_LOCK>
    class Synchronized {
        using _check = TypeSanityCheck<T>;
    public:
        template <typename... Args>
        explicit constexpr Synchronized(Args&&... args) noexcept
            : m_value(FoundationKitCxxStl::Forward<Args>(args)...) {}

        /// @brief Access the protected value via a callback (exclusive).
        template <typename Func>
        auto With(Func&& func) noexcept {
            LockGuard guard(m_lock);
            return func(m_value);
        }

        /// @brief Const access to the protected value.
        /// @desc Uses SharedLock if supported, otherwise fallbacks to exclusive LockGuard.
        template <typename Func>
        auto With(Func&& func) const noexcept {
            if constexpr (SharedLockable<LockType>) {
                SharedLock guard(m_lock);
                return func(m_value);
            } else {
                LockGuard guard(m_lock);
                return func(m_value);
            }
        }

        /// @brief Explicitly access the protected value via shared lock.
        template <typename Func>
        auto WithShared(Func&& func) const noexcept requires SharedLockable<LockType> {
            SharedLock guard(m_lock);
            return func(m_value);
        }

        // Non-copyable
        Synchronized(const Synchronized&) = delete;
        Synchronized& operator=(const Synchronized&) = delete;

    private:
        T                m_value;
        mutable LockType m_lock;
    };

} // namespace FoundationKitCxxStl::Sync
