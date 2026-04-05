#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // ControlBlock — Virtual-free, Atomic reference-counted base
    // ============================================================================

    /// @brief Internal control block for SharedPtr and WeakPtr.
    /// @desc Stores two function pointers instead of a vtable to remain compliant
    ///       with freestanding constraints. No virtual keyword appears here.
    ///       Reference counts are Atomic<usize> to be safe for concurrent sharing.
    ///
    ///       Memory ordering rationale (C++23 SharedPtr standard):
    ///       - Increment (copy ctor)  : Relaxed — only ensures the count is updated.
    ///                                             We already own a reference.
    ///       - Decrement (destructor) : AcqRel  — Release writes to the object,
    ///                                             Acquire writes from others if zero.
    ///       - Lock (Upgrade)         : Acquire — successfully seen object state.
    ///       - Load (UseCount query)  : Relaxed — observation only.
    struct ControlBlock {
        /// @brief Atomically maintained strong reference count.
        FoundationKitCxxStl::Sync::Atomic<usize> use_count{0};

        /// @brief Atomically maintained weak reference count (+1 held by SharedPtr group).
        FoundationKitCxxStl::Sync::Atomic<usize> weak_count{0};

        /// @brief Destroy the managed object (called when use_count reaches 0).
        void (*destroy_object)(ControlBlock*) noexcept;

        /// @brief Destroy this control block itself (called when weak_count reaches 0).
        void (*destroy_self)(ControlBlock*) noexcept;

        /// @brief Construct with the two function pointers set by each concrete block.
        constexpr ControlBlock(
            void (*do_obj)(ControlBlock*) noexcept,
            void (*do_self)(ControlBlock*) noexcept
        ) noexcept
            : destroy_object(do_obj), destroy_self(do_self) {}

        // Non-copyable, non-movable — always managed via pointer.
        ControlBlock(const ControlBlock&) = delete;
        ControlBlock& operator=(const ControlBlock&) = delete;
    };

    // ============================================================================
    // SeparateControlBlock<T, Alloc>
    // ============================================================================

    /// @brief Control block for an object allocated separately from the block itself.
    template <typename T, IAllocator Alloc>
    struct SeparateControlBlock final : ControlBlock {
        T*    m_ptr;
        Alloc m_alloc;

        constexpr SeparateControlBlock(T* ptr, Alloc alloc) noexcept
            : ControlBlock(&SDoDestroyObject, &SDoDestroySelf),
              m_ptr(ptr), m_alloc(FoundationKitCxxStl::Move(alloc))
        {
            use_count.Store(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
            weak_count.Store(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
        }

    private:
        static void SDoDestroyObject(ControlBlock* cb) noexcept {
            auto* self = static_cast<SeparateControlBlock*>(cb);
            if (self->m_ptr) {
                if constexpr (FoundationKitCxxStl::IsArray<T>) {
                    using ElementType = typename FoundationKitCxxStl::RemoveExtent<T>::Type;
                    const usize count = sizeof(T) / sizeof(ElementType);
                    DeleteArray(self->m_alloc, reinterpret_cast<ElementType*>(self->m_ptr), count);
                } else {
                    self->m_ptr->~T();
                    self->m_alloc.Deallocate(self->m_ptr, sizeof(T));
                }
                self->m_ptr = nullptr;
            }
        }

        static void SDoDestroySelf(ControlBlock* cb) noexcept {
            auto* self = static_cast<SeparateControlBlock*>(cb);
            Alloc alloc = FoundationKitCxxStl::Move(self->m_alloc);
            self->~SeparateControlBlock();
            alloc.Deallocate(self, sizeof(SeparateControlBlock));
        }
    };

    // ============================================================================
    // SeparateArrayControlBlock<T, Alloc>
    // ============================================================================

    /// @brief Control block for an array allocated separately with a known count.
    template <typename T, IAllocator Alloc>
    struct SeparateArrayControlBlock final : ControlBlock {
        T*    m_ptr;
        usize m_count;
        Alloc m_alloc;

        constexpr SeparateArrayControlBlock(T* ptr, const usize count, Alloc alloc) noexcept
            : ControlBlock(&SDoDestroyObject, &SDoDestroySelf),
              m_ptr(ptr), m_count(count), m_alloc(FoundationKitCxxStl::Move(alloc))
        {
            use_count.Store(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
            weak_count.Store(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
        }

    private:
        static void SDoDestroyObject(ControlBlock* cb) noexcept {
            auto* self = static_cast<SeparateArrayControlBlock*>(cb);
            if (self->m_ptr) {
                DeleteArray(self->m_alloc, self->m_ptr, self->m_count);
                self->m_ptr = nullptr;
            }
        }

        static void SDoDestroySelf(ControlBlock* cb) noexcept {
            auto* self = static_cast<SeparateArrayControlBlock*>(cb);
            Alloc alloc = FoundationKitCxxStl::Move(self->m_alloc);
            self->~SeparateArrayControlBlock();
            alloc.Deallocate(self, sizeof(SeparateArrayControlBlock));
        }
    };

    // ============================================================================
    // CombinedControlBlock<T, Alloc>
    // ============================================================================

    /// @brief Control block that holds both the ref-counts and the managed object.
    /// @desc Used by TryAllocateShared for single-allocation, cache-friendly shared ptrs.
    template <typename T, IAllocator Alloc>
    struct CombinedControlBlock final : ControlBlock {
        Alloc m_alloc;
        alignas(T) byte m_storage[sizeof(T)]{};

        template <typename... Args>
        explicit constexpr CombinedControlBlock(Alloc alloc, Args&&... args) noexcept
            : ControlBlock(&SDoDestroyObject, &SDoDestroySelf),
              m_alloc(FoundationKitCxxStl::Move(alloc))
        {
            use_count.Store(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
            weak_count.Store(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
            FoundationKitCxxStl::ConstructAt<T>(
                reinterpret_cast<T*>(m_storage),
                FoundationKitCxxStl::Forward<Args>(args)...
            );
        }

        [[nodiscard]] T* Get() noexcept {
            return reinterpret_cast<T*>(m_storage);
        }

    private:
        static void SDoDestroyObject(ControlBlock* cb) noexcept {
            auto* self = static_cast<CombinedControlBlock*>(cb);
            reinterpret_cast<T*>(self->m_storage)->~T();
        }

        static void SDoDestroySelf(ControlBlock* cb) noexcept {
            auto* self = static_cast<CombinedControlBlock*>(cb);
            Alloc alloc = FoundationKitCxxStl::Move(self->m_alloc);
            self->~CombinedControlBlock();
            alloc.Deallocate(self, sizeof(CombinedControlBlock));
        }
    };

    // Forward declaration
    template <typename T> class WeakPtr;

    // ============================================================================
    // SharedPtr<T>
    // ============================================================================

    /// @brief Reference-counted smart pointer for shared ownership.
    /// @desc Thread-safe reference counting via Atomic<usize>.
    ///       The managed object and control block need not be allocated on the same heap.
    template <typename T>
    class SharedPtr {
    public:
        using ElementType = T;

        constexpr SharedPtr() noexcept = default;
        explicit constexpr SharedPtr(nullptr_t) noexcept {}

        SharedPtr(SharedPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control)
        {
            other.m_ptr     = nullptr;
            other.m_control = nullptr;
        }

        SharedPtr(const SharedPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control)
        {
            // Relaxed: we already hold a reference to the same control block,
            // so the block cannot be destroyed while we are in this constructor.
            if (m_control)
                m_control->use_count.FetchAdd(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
        }

        ~SharedPtr() noexcept { Release(); }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr           = other.m_ptr;
                m_control       = other.m_control;
                other.m_ptr     = nullptr;
                other.m_control = nullptr;
            }
            return *this;
        }

        SharedPtr& operator=(const SharedPtr& other) noexcept {
            if (this != &other) {
                // Increment first to avoid self-destruction if other is this.
                ControlBlock* new_ctrl = other.m_control;
                if (new_ctrl)
                    new_ctrl->use_count.FetchAdd(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
                Release();
                m_ptr     = other.m_ptr;
                m_control = new_ctrl;
            }
            return *this;
        }

        void Reset() noexcept {
            Release();
            m_ptr     = nullptr;
            m_control = nullptr;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }

        [[nodiscard]] T& operator*() const noexcept {
            FK_BUG_ON(!m_ptr, "SharedPtr: dereferencing null pointer");
            return *m_ptr;
        }

        [[nodiscard]] T* operator->() const noexcept {
            FK_BUG_ON(!m_ptr, "SharedPtr: access via null pointer");
            return m_ptr;
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        [[nodiscard]] usize UseCount() const noexcept {
            return m_control
                ? m_control->use_count.Load(FoundationKitCxxStl::Sync::MemoryOrder::Relaxed)
                : 0;
        }

    private:
        template <typename U> friend class SharedPtr;
        template <typename U> friend class WeakPtr;

        template <typename U, IAllocator Alloc, typename... Args>
        friend FoundationKitCxxStl::Expected<SharedPtr<U>, MemoryError>
        TryAllocateShared(Alloc alloc, Args&&... args) noexcept;

        template <typename U, IAllocator Alloc>
        friend FoundationKitCxxStl::Expected<SharedPtr<U[]>, MemoryError>
        TryAllocateSharedArray(Alloc alloc, usize count) noexcept;

        SharedPtr(T* ptr, ControlBlock* control) noexcept
            : m_ptr(ptr), m_control(control) {}

        void Release() noexcept {
            if (!m_control) return;

            // AcqRel: Release our writes (object state), Acquire the writes of the
            // thread that decremented before us so we don't destroy a half-written object.
            const usize prev_use = m_control->use_count.FetchSub(
                1, FoundationKitCxxStl::Sync::MemoryOrder::AcqRel
            );

            if (prev_use == 1) {
                // We were the last strong owner — destroy the managed object.
                m_control->destroy_object(m_control);

                // Now release the implicit weak reference held by the SharedPtr group.
                const usize prev_weak = m_control->weak_count.FetchSub(
                    1, FoundationKitCxxStl::Sync::MemoryOrder::AcqRel
                );
                if (prev_weak == 1) {
                    // No WeakPtrs remain — destroy the control block.
                    m_control->destroy_self(m_control);
                }
            }
        }

        T*            m_ptr     = nullptr;
        ControlBlock* m_control = nullptr;
    };

    // ============================================================================
    // WeakPtr<T>
    // ============================================================================

    /// @brief Smart pointer for non-owning, observable references to shared objects.
    template <typename T>
    class WeakPtr {
    public:
        constexpr WeakPtr() noexcept = default;

        explicit WeakPtr(const SharedPtr<T>& shared) noexcept
            : m_ptr(shared.m_ptr), m_control(shared.m_control)
        {
            if (m_control)
                m_control->weak_count.FetchAdd(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
        }

        WeakPtr(const WeakPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control)
        {
            if (m_control)
                m_control->weak_count.FetchAdd(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
        }

        WeakPtr(WeakPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control)
        {
            other.m_ptr     = nullptr;
            other.m_control = nullptr;
        }

        ~WeakPtr() noexcept { Release(); }

        WeakPtr& operator=(const WeakPtr& other) noexcept {
            if (this != &other) {
                if (other.m_control)
                    other.m_control->weak_count.FetchAdd(
                        1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
                Release();
                m_ptr     = other.m_ptr;
                m_control = other.m_control;
            }
            return *this;
        }

        WeakPtr& operator=(const SharedPtr<T>& shared) noexcept {
            if (shared.m_control)
                shared.m_control->weak_count.FetchAdd(
                    1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
            Release();
            m_ptr     = shared.m_ptr;
            m_control = shared.m_control;
            return *this;
        }

        WeakPtr& operator=(WeakPtr&& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr           = other.m_ptr;
                m_control       = other.m_control;
                other.m_ptr     = nullptr;
                other.m_control = nullptr;
            }
            return *this;
        }

        /// @brief Upgrade to SharedPtr using a CAS loop to prevent TOCTOU resurrection.
        /// @desc If the last SharedPtr is concurrently destroyed between our load
        ///       and our increment, CompareExchange will fail, and we return empty.
        [[nodiscard]] SharedPtr<T> Lock() const noexcept {
            if (!m_control) return SharedPtr<T>();

            usize expected = m_control->use_count.Load(
                FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);

            while (expected != 0) {
                // Attempt to increment only if still alive.
                // Success order: Acquire — see the object's state.
                // Failure order: Relaxed — we're just reading, no dependency needed.
                if (m_control->use_count.CompareExchange(
                        expected,
                        expected + 1,
                        /*weak=*/true,
                        FoundationKitCxxStl::Sync::MemoryOrder::Acquire,
                        FoundationKitCxxStl::Sync::MemoryOrder::Relaxed))
                {
                    return SharedPtr<T>(m_ptr, m_control);
                }
                // expected was updated by CompareExchange on failure — loop.
            }
            return SharedPtr<T>(); // Object already destroyed.
        }

        [[nodiscard]] bool Expired() const noexcept {
            return !m_control ||
                   m_control->use_count.Load(FoundationKitCxxStl::Sync::MemoryOrder::Relaxed) == 0;
        }

    private:
        void Release() noexcept {
            if (!m_control) return;

            const usize prev_weak = m_control->weak_count.FetchSub(
                1, FoundationKitCxxStl::Sync::MemoryOrder::AcqRel
            );
            if (prev_weak == 1) {
                m_control->destroy_self(m_control);
            }
        }

        T*            m_ptr     = nullptr;
        ControlBlock* m_control = nullptr;
    };

    // ============================================================================
    // SharedPtr<T[]> — Array Specialisation
    // ============================================================================

    /// @brief Array specialization for SharedPtr.
    template <typename T>
    class SharedPtr<T[]> {
    public:
        using ElementType = T;

        constexpr SharedPtr() noexcept = default;
        explicit constexpr SharedPtr(nullptr_t) noexcept {}

        SharedPtr(SharedPtr&& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control)
        {
            other.m_ptr     = nullptr;
            other.m_control = nullptr;
        }

        SharedPtr(const SharedPtr& other) noexcept
            : m_ptr(other.m_ptr), m_control(other.m_control)
        {
            if (m_control)
                m_control->use_count.FetchAdd(1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
        }

        ~SharedPtr() noexcept { Release(); }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            if (this != &other) {
                Release();
                m_ptr           = other.m_ptr;
                m_control       = other.m_control;
                other.m_ptr     = nullptr;
                other.m_control = nullptr;
            }
            return *this;
        }

        SharedPtr& operator=(const SharedPtr& other) noexcept {
            if (this != &other) {
                ControlBlock* new_ctrl = other.m_control;
                if (new_ctrl)
                    new_ctrl->use_count.FetchAdd(
                        1, FoundationKitCxxStl::Sync::MemoryOrder::Relaxed);
                Release();
                m_ptr     = other.m_ptr;
                m_control = new_ctrl;
            }
            return *this;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }

        [[nodiscard]] T& operator[](usize index) const noexcept {
            FK_BUG_ON(!m_ptr, "SharedPtr[]: access via null pointer");
            return m_ptr[index];
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        [[nodiscard]] usize UseCount() const noexcept {
            return m_control
                ? m_control->use_count.Load(FoundationKitCxxStl::Sync::MemoryOrder::Relaxed)
                : 0;
        }

    private:
        template <typename U, IAllocator Alloc>
        friend FoundationKitCxxStl::Expected<SharedPtr<U[]>, MemoryError>
        TryAllocateSharedArray(Alloc alloc, usize count) noexcept;

        SharedPtr(T* ptr, ControlBlock* control) noexcept
            : m_ptr(ptr), m_control(control) {}

        void Release() noexcept {
            if (!m_control) return;

            const usize prev_use = m_control->use_count.FetchSub(
                1, FoundationKitCxxStl::Sync::MemoryOrder::AcqRel
            );
            if (prev_use == 1) {
                m_control->destroy_object(m_control);
                const usize prev_weak = m_control->weak_count.FetchSub(
                    1, FoundationKitCxxStl::Sync::MemoryOrder::AcqRel
                );
                if (prev_weak == 1)
                    m_control->destroy_self(m_control);
            }
        }

        T*            m_ptr     = nullptr;
        ControlBlock* m_control = nullptr;
    };

    // ============================================================================
    // Factory Functions
    // ============================================================================

    /// @brief Create a SharedPtr with a single allocation (control block + object combined).
    /// @tparam T   The managed type.
    /// @tparam Alloc Any IAllocator.
    /// @param alloc  Allocator (by value; moved into CombinedControlBlock).
    /// @param args   Constructor arguments for T.
    template <typename T, IAllocator Alloc, typename... Args>
    [[nodiscard]] Expected<SharedPtr<T>, MemoryError>
    TryAllocateShared(Alloc alloc, Args&&... args) noexcept {
        using CB = CombinedControlBlock<T, Alloc>;
        const AllocationResult res = alloc.Allocate(sizeof(CB), alignof(CB));
        if (!res.ok())
            return Unexpected(MemoryError::OutOfMemory);

        CB* cb = FoundationKitCxxStl::ConstructAt<CB>(
            res.ptr,
            FoundationKitCxxStl::Move(alloc),
            FoundationKitCxxStl::Forward<Args>(args)...
        );
        return SharedPtr<T>(cb->Get(), cb);
    }

    /// @brief Create a SharedPtr<T[]> for a heap-allocated array.
    template <typename T, IAllocator Alloc>
    [[nodiscard]] Expected<SharedPtr<T[]>, MemoryError>
    TryAllocateSharedArray(Alloc alloc, usize count) noexcept {
        auto arr_res = NewArray<T>(alloc, count);
        if (!arr_res)
            return static_cast<Expected<SharedPtr<T[]>, MemoryError>>(
                MemoryError::OutOfMemory);

        using CB = SeparateArrayControlBlock<T, Alloc>;
        const AllocationResult cb_res = alloc.Allocate(sizeof(CB), alignof(CB));
        if (!cb_res) {
            DeleteArray(alloc, arr_res.Value(), count);
            return static_cast<Expected<SharedPtr<T[]>, MemoryError>>(
                MemoryError::OutOfMemory);
        }

        CB* cb = FoundationKitCxxStl::ConstructAt<CB>(
            cb_res.ptr,
            arr_res.Value(),
            count,
            FoundationKitCxxStl::Move(alloc)
        );
        return SharedPtr<T[]>(arr_res.Value(), cb);
    }

    // ============================================================================
    // Static Assertions
    // ============================================================================

    // ControlBlock must be standard-layout (no implicit vptr, no virtual bases).
    // This is the freestanding-safe invariant: if ControlBlock had any virtual member,
    // it would not be standard-layout and this assert would fire.
    static_assert(__is_standard_layout(ControlBlock),
        "ControlBlock must be standard-layout (no vptr, no virtual dispatch)");

} // namespace FoundationKitMemory

// ============================================================================
// Formatter Specializations
// ============================================================================

namespace FoundationKitCxxStl {

    template <typename T>
    struct Formatter<FoundationKitMemory::SharedPtr<T>> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitMemory::SharedPtr<T>& value, const FormatSpec& spec = {}) const noexcept {
            sb.Append("SharedPtr(");
            Formatter<T*>().Format(sb, value.Get(), spec);
            sb.Append(')');
        }
    };

    template <typename T>
    struct Formatter<FoundationKitMemory::SharedPtr<T[]>> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitMemory::SharedPtr<T[]>& value, const FormatSpec& spec = {}) const noexcept {
            sb.Append("SharedPtr[](");
            Formatter<T*>().Format(sb, value.Get(), spec);
            sb.Append(')');
        }
    };

    template <typename T>
    struct Formatter<FoundationKitMemory::WeakPtr<T>> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitMemory::WeakPtr<T>& value, const FormatSpec& spec = {}) const noexcept {
            sb.Append("WeakPtr(");
            if (value.Expired()) {
                sb.Append("expired", 7);
            } else {
                Formatter<T*>().Format(sb, value.Lock().Get(), spec);
            }
            sb.Append(')');
        }
    };

} // namespace FoundationKitCxxStl
