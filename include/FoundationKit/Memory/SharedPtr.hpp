#pragma once

#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Base/Utility.hpp>

namespace FoundationKit::Memory {

    /// @brief Internal control block for SharedPtr and WeakPtr.
    /// Handles destruction of the managed object and itself.
    struct ControlBlock {
        usize use_count{1};
        usize weak_count{1};

        virtual ~ControlBlock() = default;

        /// @brief Destructs the managed object.
        virtual void DestroyObject() noexcept = 0;

        /// @brief Deallocates the control block itself.
        virtual void DestroySelf() noexcept = 0;
    };

    /// @brief Control block for an object allocated separately from the ref-count.
    template <typename T, IAllocator Alloc>
    struct SeparateControlBlock final : ControlBlock {
        T* m_ptr;
        Alloc m_alloc;

        constexpr SeparateControlBlock(T* ptr, Alloc alloc) noexcept 
            : m_ptr(ptr), m_alloc(alloc) {}

        void DestroyObject() noexcept override {
            if (m_ptr) {
                m_ptr->~T();
                m_alloc.Deallocate(m_ptr, sizeof(T));
                m_ptr = nullptr;
            }
        }

        void DestroySelf() noexcept override {
            Alloc alloc = m_alloc;
            this->~SeparateControlBlock();
            alloc.Deallocate(this, sizeof(SeparateControlBlock));
        }
    };

    /// @brief Control block for an object allocated together with the ref-count.
    template <typename T, IAllocator Alloc>
    struct CombinedControlBlock final :  ControlBlock {
        Alloc m_alloc;
        alignas(T) byte m_storage[sizeof(T)]{};

        template <typename... Args>
        explicit constexpr CombinedControlBlock(Alloc alloc, Args&&... args) noexcept
            : m_alloc(alloc) {
            ::new (m_storage) T(Forward<Args>(args)...);
        }

        void DestroyObject() noexcept override {
            reinterpret_cast<T*>(m_storage)->~T();
        }

        void DestroySelf() noexcept override {
            Alloc alloc = m_alloc;
            this->~CombinedControlBlock();
            alloc.Deallocate(this, sizeof(CombinedControlBlock));
        }

        [[nodiscard]] T* Get() noexcept { return reinterpret_cast<T*>(m_storage); }
    };

    template <typename T>
    class WeakPtr;

    /// @brief Reference-counted smart pointer for shared ownership.
    template <typename T>
    class SharedPtr {
    public:
        constexpr SharedPtr() noexcept = default;

        explicit constexpr SharedPtr(nullptr_t) noexcept {}

        /// @brief Move constructor.
        SharedPtr(SharedPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            other.m_ptr = nullptr;
            other.m_control = nullptr;
        }

        /// @brief Copy constructor.
        SharedPtr(const SharedPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            if (m_control) {
                m_control->use_count++;
            }
        }

        /// @brief Aliasing constructor.
        template <typename U>
        SharedPtr(const SharedPtr<U>& other, T* ptr) noexcept
            : m_ptr(ptr), m_control(other.m_control) {
            if (m_control) {
                m_control->use_count++;
            }
        }

        ~SharedPtr() noexcept {
            Release();
        }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr = other.m_ptr;
                m_control = other.m_control;
                other.m_ptr = nullptr;
                other.m_control = nullptr;
            }
            return *this;
        }

        SharedPtr& operator=(const SharedPtr& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr = other.m_ptr;
                m_control = other.m_control;
                if (m_control) {
                    m_control->use_count++;
                }
            }
            return *this;
        }

        void Reset() noexcept {
            Release();
            m_ptr = nullptr;
            m_control = nullptr;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator*()  const noexcept { return *m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        [[nodiscard]] usize UseCount() const noexcept {
            return m_control ? m_control->use_count : 0;
        }

    private:
        template <typename U> friend class SharedPtr;
        template <typename U> friend class WeakPtr;

        template <typename U, IAllocator Alloc, typename... Args>
        friend SharedPtr<U> AllocateShared(Alloc alloc, Args&&... args) noexcept;

        template <typename U, IAllocator Alloc>
        friend SharedPtr<U> MakeSharedFromPointer(Alloc alloc, U* ptr) noexcept;

        // Internal constructor used by MakeShared/AllocateShared
        SharedPtr(T* ptr, ControlBlock* control) noexcept 
            : m_ptr(ptr), m_control(control) {}

        void Release() const noexcept {
            if (m_control) {
                if (--m_control->use_count == 0) {
                    m_control->DestroyObject();
                    if (--m_control->weak_count == 0) {
                        m_control->DestroySelf();
                    }
                }
            }
        }

        T* m_ptr = nullptr;
        ControlBlock* m_control = nullptr;
    };

    /// @brief Smart pointer for non-owning, observable references to shared objects.
    template <typename T>
    class WeakPtr {
    public:
        constexpr WeakPtr() noexcept = default;

        explicit WeakPtr(const SharedPtr<T>& shared) noexcept
            : m_ptr(shared.m_ptr), m_control(shared.m_control) {
            if (m_control) {
                m_control->weak_count++;
            }
        }

        WeakPtr(const WeakPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            if (m_control) {
                m_control->weak_count++;
            }
        }

        WeakPtr(WeakPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            other.m_ptr = nullptr;
            other.m_control = nullptr;
        }

        ~WeakPtr() noexcept {
            Release();
        }

        WeakPtr& operator=(const WeakPtr& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr = other.m_ptr;
                m_control = other.m_control;
                if (m_control) {
                    m_control->weak_count++;
                }
            }
            return *this;
        }

        WeakPtr& operator=(const SharedPtr<T>& shared) noexcept {
            Release();
            m_ptr = shared.m_ptr;
            m_control = shared.m_control;
            if (m_control) {
                m_control->weak_count++;
            }
            return *this;
        }

        WeakPtr& operator=(WeakPtr&& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr = other.m_ptr;
                m_control = other.m_control;
                other.m_ptr = nullptr;
                other.m_control = nullptr;
            }
            return *this;
        }

        [[nodiscard]] SharedPtr<T> Lock() const noexcept {
            if (m_control && m_control->use_count > 0) {
                m_control->use_count++;
                return SharedPtr<T>(m_ptr, m_control);
            }
            return SharedPtr<T>();
        }

        [[nodiscard]] bool Expired() const noexcept {
            return !m_control || m_control->use_count == 0;
        }

    private:
        void Release() const noexcept {
            if (m_control) {
                if (--m_control->weak_count == 0) {
                    m_control->DestroySelf();
                }
            }
        }

        T* m_ptr = nullptr;
        ControlBlock* m_control = nullptr;
    };

    /// @brief Creates a SharedPtr with an object and its control block in one allocation.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]] SharedPtr<T> AllocateShared(Alloc alloc, Args&&... args) noexcept {
        using CB = CombinedControlBlock<T, Alloc>;
        AllocResult res = alloc.Allocate(sizeof(CB), alignof(CB));
        if (!res) return SharedPtr<T>();

        CB* cb = ::new (res.ptr) CB(alloc, Forward<Args>(args)...);
        return SharedPtr<T>(cb->Get(), cb);
    }

    /// @brief Helper for wrapping an existing pointer into a SharedPtr.
    template <typename T, IAllocator Alloc>
    [[nodiscard]] SharedPtr<T> MakeSharedFromPointer(Alloc alloc, T* ptr) noexcept {
        if (!ptr) return SharedPtr<T>();

        using CB = SeparateControlBlock<T, Alloc>;
        AllocResult res = alloc.Allocate(sizeof(CB), alignof(CB));
        if (!res) {
            Delete(alloc, ptr);
            return SharedPtr<T>();
        }

        CB* cb = ::new (res.ptr) CB(ptr, alloc);
        return SharedPtr<T>(ptr, cb);
    }

} // namespace FoundationKit::Memory
