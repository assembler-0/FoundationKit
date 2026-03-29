#pragma once

#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/Expected.hpp>

namespace FoundationKit::Memory {

    /// @brief Internal control block for SharedPtr and WeakPtr.
    struct ControlBlock {
        usize use_count{0};
        usize weak_count{0};

        virtual ~ControlBlock() = default;
        virtual void DestroyObject() noexcept = 0;
        virtual void DestroySelf() noexcept = 0;
    };

    /// @brief Control block for an object allocated separately.
    template <typename T, IAllocator Alloc>
    struct SeparateControlBlock final : ControlBlock {
        T* m_ptr;
        Alloc m_alloc;

        constexpr SeparateControlBlock(T* ptr, Alloc alloc) noexcept 
            : m_ptr(ptr), m_alloc(alloc) {
            this->use_count = 1;
            this->weak_count = 1;
        }

        void DestroyObject() noexcept override {
            if (m_ptr) {
                if constexpr (IsArray<T>) {
                    static_assert(sizeof(T) > 0+0, "FoundationKit: SharedPtr managed array must have a known size at compile time if count is not provided.");
                    using ElementType = RemoveExtent<T>::Type;
                    const usize count = sizeof(T) / sizeof(ElementType);
                    DeleteArray(m_alloc, reinterpret_cast<ElementType*>(m_ptr), count);
                } else {
                    m_ptr->~T();
                    m_alloc.Deallocate(m_ptr, sizeof(T));
                }
                m_ptr = nullptr;
            }
        }

        void DestroySelf() noexcept override {
            Alloc alloc = m_alloc;
            this->~SeparateControlBlock();
            alloc.Deallocate(this, sizeof(SeparateControlBlock));
        }
    };

    /// @brief Control block for an array allocated separately.
    template <typename T, IAllocator Alloc>
    struct SeparateArrayControlBlock final : ControlBlock {
        T*    m_ptr;
        usize m_count;
        Alloc m_alloc;

        constexpr SeparateArrayControlBlock(T* ptr, const usize count, Alloc alloc) noexcept
            : m_ptr(ptr), m_count(count), m_alloc(alloc) {
            this->use_count = 1;
            this->weak_count = 1;
        }

        void DestroyObject() noexcept override {
            if (m_ptr) {
                for (usize i = m_count; i-- > 0;) m_ptr[i].~T();
                m_alloc.Deallocate(m_ptr, sizeof(T) * m_count);
                m_ptr = nullptr;
            }
        }

        void DestroySelf() noexcept override {
            Alloc alloc = m_alloc;
            this->~SeparateArrayControlBlock();
            alloc.Deallocate(this, sizeof(SeparateArrayControlBlock));
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
            this->use_count = 1;
            this->weak_count = 1;
            FoundationKit::ConstructAt<T>(reinterpret_cast<T*>(m_storage), Forward<Args>(args)...);
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

    template <typename T> class WeakPtr;

    /// @brief Reference-counted smart pointer for shared ownership.
    template <typename T>
    class SharedPtr {
    public:
        using ElementType = T;

        constexpr SharedPtr() noexcept = default;
        explicit constexpr SharedPtr(nullptr_t) noexcept {}

        SharedPtr(SharedPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            other.m_ptr = nullptr;
            other.m_control = nullptr;
        }

        SharedPtr(const SharedPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            if (m_control) m_control->use_count++;
        }

        ~SharedPtr() noexcept { Release(); }

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
                if (m_control) m_control->use_count++;
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
        [[nodiscard]] usize UseCount() const noexcept { return m_control ? m_control->use_count : 0; }

    private:
        template <typename U> friend class SharedPtr;
        template <typename U> friend class WeakPtr;

        template <typename U, IAllocator Alloc, typename... Args>
        friend Expected<SharedPtr<U>, MemoryError> TryAllocateShared(Alloc alloc, Args&&... args) noexcept;

        template <typename U, IAllocator Alloc>
        friend Expected<SharedPtr<U[]>, MemoryError> TryAllocateSharedArray(Alloc alloc, usize count) noexcept;

        // Internal constructor that ADOPTS the reference count from factory
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

        T*            m_ptr     = nullptr;
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

    /// @brief Array specialization for SharedPtr.
    template <typename T>
    class SharedPtr<T[]> {
    public:
        using ElementType = T;

        constexpr SharedPtr() noexcept = default;
        explicit constexpr SharedPtr(nullptr_t) noexcept {}

        SharedPtr(SharedPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            other.m_ptr = nullptr;
            other.m_control = nullptr;
        }

        SharedPtr(const SharedPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control) {
            if (m_control) m_control->use_count++;
        }

        ~SharedPtr() noexcept { Release(); }

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
                if (m_control) m_control->use_count++;
            }
            return *this;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator[](usize index) const noexcept { return m_ptr[index]; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }
        [[nodiscard]] usize UseCount() const noexcept { return m_control ? m_control->use_count : 0; }

    private:
        template <typename U, IAllocator Alloc>
        friend Expected<SharedPtr<U[]>, MemoryError> TryAllocateSharedArray(Alloc alloc, usize count) noexcept;

        SharedPtr(T* ptr, ControlBlock* control) noexcept 
            : m_ptr(ptr), m_control(control) {}

        void Release() const noexcept {
            if (m_control) {
                if (--m_control->use_count == 0) {
                    m_control->DestroyObject();
                    if (--m_control->weak_count == 0) m_control->DestroySelf();
                }
            }
        }

        T*            m_ptr     = nullptr;
        ControlBlock* m_control = nullptr;
    };

    /// @brief Factory methods.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]] Expected<SharedPtr<T>, MemoryError> TryAllocateShared(Alloc alloc, Args&&... args) noexcept {
        using CB = CombinedControlBlock<T, Alloc>;
        AllocResult res = alloc.Allocate(sizeof(CB), alignof(CB));
        if (!res) return MemoryError::OutOfMemory;

        CB* cb = ::new (res.ptr) CB(alloc, Forward<Args>(args)...);
        return SharedPtr<T>(cb->Get(), cb);
    }

    template <typename T, IAllocator Alloc>
    [[nodiscard]] Expected<SharedPtr<T[]>, MemoryError> TryAllocateSharedArray(Alloc alloc, usize count) noexcept {
        auto arr_res = NewArray<T>(alloc, count);
        if (!arr_res) return MemoryError::OutOfMemory;

        using CB = SeparateArrayControlBlock<T, Alloc>;
        AllocResult cb_res = alloc.Allocate(sizeof(CB), alignof(CB));
        if (!cb_res) {
            DeleteArray(alloc, arr_res.Value(), count);
            return MemoryError::OutOfMemory;
        }

        CB* cb = ::new (cb_res.ptr) CB(arr_res.Value(), count, alloc);
        return SharedPtr<T[]>(arr_res.Value(), cb);
    }

} // namespace FoundationKit::Memory
