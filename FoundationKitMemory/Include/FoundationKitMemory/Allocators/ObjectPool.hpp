#pragma once

#include <FoundationKitMemory/Core/MemoryObject.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>

namespace FoundationKitMemory {

    // =========================================================================
    // ObjectPool<T, Alloc>
    // =========================================================================

    /// @brief A typed, intrusive, dynamically-backed object pool for a single type T.
    ///
    /// @desc  Replaces ObjectAllocator for the common single-type case.
    ///        Key properties vs ObjectAllocator:
    ///          - No m_heads[MaxTypes] array: zero per-type pointer overhead.
    ///          - T::kObjectType is verified at compile time via IMemoryObject<T>.
    ///          - ForEach walks only live objects of T via an intrusive live-list.
    ///          - Each allocation prepends a SlotHeader (two pointers) before the
    ///            T payload. When the slot is on the free-list, the same storage
    ///            holds a free-list pointer — no separate bookkeeping array.
    ///
    ///        Memory layout per slot:
    ///          [SlotHeader: live_next* | free_next* (union)]
    ///          [T payload — aligned to alignof(T)]
    ///
    ///        Thread safety: NOT thread-safe. Wrap the backing allocator with
    ///        SynchronizedAllocator if concurrent access is required.
    ///
    /// @tparam T     Must satisfy IMemoryObject<T>.
    /// @tparam Alloc Backing IAllocator. Passed by reference — must outlive the pool.
    template <typename T, IAllocator Alloc>
        requires IMemoryObject<T>
    class ObjectPool {
    public:
        // ----------------------------------------------------------------
        // Internal slot layout
        // ----------------------------------------------------------------
        /// @brief Header prepended to every slot.
        struct SlotHeader {
            struct LivePointers {
                SlotHeader* next; ///< Next live slot.
                SlotHeader* prev; ///< Previous live slot (enables O(1) Deallocate).
            };

            union {
                LivePointers live;      ///< Live-list pointers.
                SlotHeader*  free_next; ///< Next free slot (valid when on free-list).
            } u;
        };

        static constexpr usize kSlotAlign  = alignof(T) > alignof(SlotHeader)
                                             ? alignof(T) : alignof(SlotHeader);
        static constexpr usize kHeaderSize = sizeof(SlotHeader);
        // Pad header so that the T payload is aligned to kSlotAlign.
        static constexpr usize kHeaderPad  = kHeaderSize % kSlotAlign == 0
                                             ? 0
                                             : kSlotAlign - kHeaderSize % kSlotAlign;
        static constexpr usize kSlotSize   = kHeaderSize + kHeaderPad + sizeof(T);

        // ----------------------------------------------------------------
        // Construction
        // ----------------------------------------------------------------

        /// @param alloc  Backing allocator reference. Must outlive this pool.
        explicit constexpr ObjectPool(Alloc& alloc) noexcept
            : m_alloc(alloc) {}

        ObjectPool(const ObjectPool&)            = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        ~ObjectPool() noexcept {
            // Crash if any objects are still live — caller must Deallocate all
            // objects before destroying the pool.
            FK_BUG_ON(m_live_head != nullptr,
                "ObjectPool::~ObjectPool: destroyed with {} live object(s) of type 0x{:04x} "
                "— all objects must be Deallocated before the pool is destroyed",
                m_count, static_cast<u16>(T::kObjectType));
        }

        // ----------------------------------------------------------------
        // Allocate
        // ----------------------------------------------------------------

        /// @brief Construct a T in a new slot, returning Expected<T*, MemoryError>.
        /// @param flags  Allocation attribute flags (e.g. Zeroed, Pinned).
        /// @param args   Arguments forwarded to T's constructor.
        template <typename... Args>
        [[nodiscard]] Expected<T*, MemoryError>
        Allocate(const MemoryObjectFlags flags, Args&&... args) noexcept {
            void* raw = nullptr;

            if (m_free_head) {
                // Recycle a previously freed slot — O(1), no allocator call.
                SlotHeader* slot = m_free_head;
                m_free_head      = slot->u.free_next;
                raw              = slot;
            } else {
                const AllocationResult res = m_alloc.Allocate(kSlotSize, kSlotAlign);
                if (!res) return Unexpected(MemoryError::OutOfMemory);
                raw = res.ptr;
            }

            auto* hdr = static_cast<SlotHeader*>(raw);

            T* payload = reinterpret_cast<T*>(
                static_cast<byte*>(raw) + kHeaderSize + kHeaderPad);

            if (HasFlag(flags, MemoryObjectFlags::Zeroed)) {
                MemoryZero(payload, sizeof(T));
            }

            FoundationKitCxxStl::ConstructAt<T>(
                payload, FoundationKitCxxStl::Forward<Args>(args)...);

            // Link into live-list (prepend — O(1)).
            hdr->u.live.next = m_live_head;
            hdr->u.live.prev = nullptr;
            if (m_live_head) {
                m_live_head->u.live.prev = hdr;
            }
            m_live_head    = hdr;
            ++m_count;

            return payload;
        }

        /// @brief Convenience overload: Allocate with MemoryObjectFlags::None.
        template <typename... Args>
        [[nodiscard]] Expected<T*, MemoryError>
        Allocate(Args&&... args) noexcept {
            return Allocate(MemoryObjectFlags::None,
                            FoundationKitCxxStl::Forward<Args>(args)...);
        }

        // ----------------------------------------------------------------
        // Deallocate
        // ----------------------------------------------------------------

        /// @brief Destroy T and return its slot to the free-list.
        /// @param ptr  Pointer previously returned by Allocate(). Must not be null.
        void Deallocate(T* ptr) noexcept {
            FK_BUG_ON(ptr == nullptr,
                "ObjectPool::Deallocate: null pointer passed");
            FK_BUG_ON(m_live_head == nullptr,
                "ObjectPool::Deallocate: pool has no live objects — double-free or corruption");

            auto* hdr = reinterpret_cast<SlotHeader*>(
                reinterpret_cast<byte*>(ptr) - kHeaderPad - kHeaderSize);

            // Unlink from live-list — O(1).
            if (hdr->u.live.prev) {
                hdr->u.live.prev->u.live.next = hdr->u.live.next;
            } else {
                m_live_head = hdr->u.live.next;
            }

            if (hdr->u.live.next) {
                hdr->u.live.next->u.live.prev = hdr->u.live.prev;
            }

            --m_count;

            ptr->~T();

            // Push onto free-list — O(1).
            hdr->u.free_next = m_free_head;
            m_free_head      = hdr;
        }

        // ----------------------------------------------------------------
        // ForEach
        // ----------------------------------------------------------------

        /// @brief Invoke func(T*) for every live object in the pool.
        /// @desc  Iteration order is reverse-allocation (LIFO).
        ///        Do NOT Allocate or Deallocate inside func — doing so mutates
        ///        the live-list under iteration.
        /// @tparam Func  Callable with signature void(T*).
        template <typename Func>
        void ForEach(Func&& func) noexcept {
            SlotHeader* hdr = m_live_head;
            while (hdr) {
                // Capture next before func() — func must not deallocate, but
                // we read next first as a defensive measure.
                SlotHeader* next = hdr->u.live.next;
                T* obj = reinterpret_cast<T*>(
                    reinterpret_cast<byte*>(hdr) + kHeaderSize + kHeaderPad);
                func(obj);
                hdr = next;
            }
        }

        // ----------------------------------------------------------------
        // Queries
        // ----------------------------------------------------------------

        /// @brief Number of currently live (allocated) objects.
        [[nodiscard]] usize Count() const noexcept { return m_count; }

        /// @brief True if no objects are currently live.
        [[nodiscard]] bool Empty() const noexcept { return m_count == 0; }

        /// @brief The MemoryObjectType tag this pool manages.
        [[nodiscard]] static constexpr MemoryObjectType ObjectType() noexcept {
            return T::kObjectType;
        }

    private:
        Alloc&      m_alloc     = {};
        SlotHeader* m_live_head = nullptr; ///< Head of the live-object intrusive list.
        SlotHeader* m_free_head = nullptr; ///< Head of the recycled-slot free-list.
        usize       m_count     = 0;       ///< Number of live objects.
    };

} // namespace FoundationKitMemory
