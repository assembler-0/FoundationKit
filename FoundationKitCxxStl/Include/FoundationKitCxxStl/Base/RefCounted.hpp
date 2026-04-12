#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include "FoundationKitCxxStl/Base/Safety.hpp"

namespace FoundationKitCxxStl {

    /// @brief Base class for intrusive reference counting operations.
    ///
    /// @desc  Provides thread-safe AddRef() and ReleaseRef() using Atomics.
    ///        Starts with a reference count of 1.
    class RefCountedBase {
    public:
        constexpr RefCountedBase() noexcept : m_ref_count(1) {}

        RefCountedBase(const RefCountedBase &) = delete;
        RefCountedBase &operator=(const RefCountedBase &) = delete;

        void AddRef() const noexcept { m_ref_count.FetchAdd(1, Sync::MemoryOrder::Relaxed); }

        /// @brief Decrements the reference count.
        /// @return true if the reference count reached zero.
        [[nodiscard]] bool ReleaseRef() const noexcept {
            FK_BUG_ON(m_ref_count.Load(Sync::MemoryOrder::Relaxed) == 0, "RefCountedBase: Double free or underflow");

            if (m_ref_count.FetchSub(1, Sync::MemoryOrder::AcqRel) == 1) {
                return true;
            }
            return false;
        }

        [[nodiscard]] u32 GetRefCount() const noexcept { return m_ref_count.Load(Sync::MemoryOrder::Acquire); }

    private:
        mutable Sync::Atomic<u32> m_ref_count;
    };

    /// @brief CRTP RefCounted helper.
    ///
    /// @desc  Inherit from this to provide default IntrusivePtrAddRef/Release implementations.
    ///        Note: If a class needs custom destruction logic upon reaching zero,
    ///        it should override the `Release` behavior.
    template<typename T>
    class RefCounted : public RefCountedBase {
        using _check = TypeSanityCheck<T>;

    public:
        constexpr RefCounted() noexcept = default;

        void AddRef() const noexcept { RefCountedBase::AddRef(); }

        void Release() const noexcept {
            if (RefCountedBase::ReleaseRef()) {
                // In a freestanding environment, we expect the derived class
                // or a custom allocator pool to handle memory reclamation.
                // Standard delete cannot be used here directly.
                // We static_cast to T* and call a custom Destroy() if it exists.
                static_cast<const T *>(this)->Destroy();
            }
        }
    };

} // namespace FoundationKitCxxStl
