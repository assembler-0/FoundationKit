#pragma once

#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>

namespace FoundationKitCxxStl {

    // Intrusive smart pointer hooks hooks
    template<typename T>
    constexpr void IntrusivePtrAddRef(T* p) noexcept {
        p->AddRef();
    }

    template<typename T>
    constexpr void IntrusivePtrRelease(T* p) noexcept {
        p->Release();
    }

    /// @brief A freestanding intrusive reference-counted pointer.
    ///
    /// @desc  Requires the managed type to conform to the AddRef()/Release() pattern
    ///        either directly or via the global IntrusivePtr* hooks.
    template <typename T>
    class RefPtr {
        using _check = TypeSanityCheck<T>;
    public:
        using ElementType = T;

        constexpr RefPtr() noexcept : m_ptr(nullptr) {}
        constexpr RefPtr(decltype(nullptr)) noexcept : m_ptr(nullptr) {}

        /// @brief Takes ownership of `p`. Does NOT call AddRef.
        /// @param p The raw pointer to manage, assumed to have ref_count == 1.
        explicit RefPtr(T* p) noexcept : m_ptr(p) {}

        RefPtr(const RefPtr& other) noexcept : m_ptr(other.m_ptr) {
            if (m_ptr) {
                IntrusivePtrAddRef(m_ptr);
            }
        }

        template <typename U>
        RefPtr(const RefPtr<U>& other) noexcept : m_ptr(other.Get()) {
            if (m_ptr) {
                IntrusivePtrAddRef(m_ptr);
            }
        }

        RefPtr(RefPtr&& other) noexcept : m_ptr(other.m_ptr) {
            other.m_ptr = nullptr;
        }

        template <typename U>
        RefPtr(RefPtr<U>&& other) noexcept : m_ptr(other.Leak()) {
        }

        ~RefPtr() noexcept {
            if (m_ptr) {
                IntrusivePtrRelease(m_ptr);
            }
        }

        RefPtr& operator=(const RefPtr& other) noexcept {
            RefPtr(other).Swap(*this);
            return *this;
        }

        RefPtr& operator=(RefPtr&& other) noexcept {
            RefPtr(Move(other)).Swap(*this);
            return *this;
        }

        template <typename U>
        RefPtr& operator=(const RefPtr<U>& other) noexcept {
            RefPtr<T>(other).Swap(*this);
            return *this;
        }

        template <typename U>
        RefPtr& operator=(RefPtr<U>&& other) noexcept {
            RefPtr<T>(Move(other)).Swap(*this);
            return *this;
        }

        void Reset(T* p = nullptr) noexcept {
            RefPtr(p).Swap(*this);
        }

        [[nodiscard]] T* Leak() noexcept {
            T* ptr = m_ptr;
            m_ptr = nullptr;
            return ptr;
        }

        void Swap(RefPtr& other) noexcept {
            T* tmp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = tmp;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator*() const noexcept { return *m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

    private:
        T* m_ptr;
    };

    template <typename T, typename U>
    [[nodiscard]] constexpr bool operator==(const RefPtr<T>& a, const RefPtr<U>& b) noexcept {
        return a.Get() == b.Get();
    }

    template <typename T, typename U>
    [[nodiscard]] constexpr bool operator!=(const RefPtr<T>& a, const RefPtr<U>& b) noexcept {
        return a.Get() != b.Get();
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator==(const RefPtr<T>& a, decltype(nullptr)) noexcept {
        return a.Get() == nullptr;
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator!=(const RefPtr<T>& a, decltype(nullptr)) noexcept {
        return a.Get() != nullptr;
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator==(decltype(nullptr), const RefPtr<T>& b) noexcept {
        return nullptr == b.Get();
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator!=(decltype(nullptr), const RefPtr<T>& b) noexcept {
        return nullptr != b.Get();
    }

} // namespace FoundationKitCxxStl
