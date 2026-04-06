#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    // =========================================================================
    // LazyInit<T> — deferred, one-shot initialisation wrapper
    //
    // Kernel subsystems initialise in a strict order.  LazyInit<T> holds
    // uninitialized aligned storage and enforces:
    //   - FK_BUG_ON any Get() / operator* / operator-> before Init() is called.
    //   - FK_BUG_ON double-Init() (second call after the object is live).
    //
    // The destructor calls T::~T() only if Init() was called, so it is safe
    // to place LazyInit<T> in BSS (zero-initialised) — m_initialised starts
    // as false.
    //
    // Thread-safety: Init() and Get() are NOT thread-safe by design.  The
    // kernel init path is single-threaded; if concurrent init is needed, wrap
    // with a SpinLock at the call site.
    // =========================================================================

    /// @brief Deferred, one-shot initialisation wrapper.
    /// @tparam T The type to lazily construct. Must pass TypeSanityCheck.
    template <typename T>
    class LazyInit {
        using _check = TypeSanityCheck<T>;
    public:
        constexpr LazyInit() noexcept : m_initialised(false) {}

        ~LazyInit() noexcept {
            if (m_initialised) Get().~T();
        }

        LazyInit(const LazyInit&)            = delete;
        LazyInit& operator=(const LazyInit&) = delete;
        LazyInit(LazyInit&&)                 = delete;
        LazyInit& operator=(LazyInit&&)      = delete;

        /// @brief Construct T in-place. FK_BUG_ON if called more than once.
        /// @param args Constructor arguments forwarded to T.
        template <typename... Args>
        void Init(Args&&... args) noexcept {
            FK_BUG_ON(m_initialised,
                "LazyInit::Init: double-initialisation detected — "
                "Init() was already called for this instance");
            ConstructAt<T>(Storage(), Forward<Args>(args)...);
            m_initialised = true;
        }

        /// @brief Access the contained value. FK_BUG_ON if not yet initialised.
        [[nodiscard]] T& Get() noexcept {
            FK_BUG_ON(!m_initialised,
                "LazyInit::Get: accessed before Init() was called");
            return *reinterpret_cast<T*>(m_storage);
        }

        [[nodiscard]] const T& Get() const noexcept {
            FK_BUG_ON(!m_initialised,
                "LazyInit::Get: accessed before Init() was called");
            return *reinterpret_cast<const T*>(m_storage);
        }

        [[nodiscard]] T&       operator*()        noexcept { return Get(); }
        [[nodiscard]] const T& operator*()  const noexcept { return Get(); }
        [[nodiscard]] T*       operator->()       noexcept { return &Get(); }
        [[nodiscard]] const T* operator->() const noexcept { return &Get(); }

        [[nodiscard]] bool IsInitialised() const noexcept { return m_initialised; }

    private:
        [[nodiscard]] void* Storage() noexcept {
            return reinterpret_cast<void*>(m_storage);
        }

        // alignas(T) ensures the raw buffer satisfies T's alignment requirement.
        // sizeof(T) bytes are sufficient by definition.
        alignas(T) unsigned char m_storage[sizeof(T)];
        bool m_initialised;
    };

} // namespace FoundationKitCxxStl
