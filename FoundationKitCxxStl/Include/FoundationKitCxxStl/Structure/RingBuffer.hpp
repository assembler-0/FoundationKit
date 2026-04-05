#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // =========================================================================
    // StaticRingBuffer<T, N> — SPSC, lock-free, ISR-safe producer
    //
    // N MUST be a power of two. This replaces the modulo operation with a
    // bitwise AND on the index mask, which is critical in ISR context where
    // division instructions may be slow or unavailable on some architectures.
    //
    // Memory ordering rationale:
    //   Push (producer/ISR):
    //     - Reads m_head with Relaxed: only the producer writes m_head, so no
    //       synchronisation with itself is needed.
    //     - Reads m_tail with Acquire: pairs with the Release store in Pop so
    //       that the producer sees the most recent consumer progress before
    //       deciding the buffer is full.
    //     - Writes m_head with Release: makes the newly written slot visible to
    //       the consumer before the head index is advanced.
    //   Pop (consumer/thread):
    //     - Reads m_tail with Relaxed: only the consumer writes m_tail.
    //     - Reads m_head with Acquire: pairs with the Release store in Push so
    //       the consumer sees the slot data before reading it.
    //     - Writes m_tail with Release: makes consumer progress visible to the
    //       producer so it can reclaim the slot.
    // =========================================================================

    /// @brief Single-producer / single-consumer lock-free ring buffer.
    ///
    /// @tparam T  Element type. Must be TriviallyCopyable — no destructor is
    ///            called on overwritten slots, and slots are copied with a plain
    ///            assignment. This is a hard requirement for ISR safety.
    /// @tparam N  Capacity. Must be a non-zero power of two.
    template <TriviallyCopyable T, usize N>
    class StaticRingBuffer {
        static_assert((N & (N - 1)) == 0 && N > 0,
            "StaticRingBuffer: N must be a non-zero power of two (enables lock-free index masking)");
        using _check = TypeSanityCheck<T>;

        static constexpr usize kMask = N - 1;

    public:
        StaticRingBuffer() noexcept : m_head(0), m_tail(0) {}

        // Non-copyable: two copies sharing the same logical ring is a logic error.
        StaticRingBuffer(const StaticRingBuffer&)            = delete;
        StaticRingBuffer& operator=(const StaticRingBuffer&) = delete;

        /// @brief Push an element from the producer (ISR-safe, no locks).
        /// @return true if the element was enqueued; false if the buffer is full.
        [[nodiscard]] bool Push(const T& value) noexcept {
            const usize head = m_head.Load(MemoryOrder::Relaxed);
            const usize next = (head + 1) & kMask;
            // Acquire: see the latest tail written by the consumer.
            if (next == m_tail.Load(MemoryOrder::Acquire)) return false; // full
            m_slots[head] = value;
            // Release: make the slot write visible before advancing the head.
            m_head.Store(next, MemoryOrder::Release);
            return true;
        }

        /// @brief Pop an element from the consumer (thread context only).
        /// @return The element, or an empty Optional if the buffer is empty.
        [[nodiscard]] Optional<T> Pop() noexcept {
            const usize tail = m_tail.Load(MemoryOrder::Relaxed);
            // Acquire: see the slot data written by the producer before reading it.
            if (tail == m_head.Load(MemoryOrder::Acquire)) return {}; // empty
            T value = m_slots[tail];
            // Release: make consumer progress visible to the producer.
            m_tail.Store((tail + 1) & kMask, MemoryOrder::Release);
            return value;
        }

        /// @brief Returns true if the buffer currently contains no elements.
        [[nodiscard]] bool Empty() const noexcept {
            return m_head.Load(MemoryOrder::Acquire) == m_tail.Load(MemoryOrder::Acquire);
        }

        /// @brief Returns the number of elements currently in the buffer.
        ///        This is a snapshot; the value may change immediately after return.
        [[nodiscard]] usize Size() const noexcept {
            const usize h = m_head.Load(MemoryOrder::Acquire);
            const usize t = m_tail.Load(MemoryOrder::Acquire);
            return (h - t + N) & kMask;
        }

        static constexpr usize Capacity() noexcept { return N - 1; }

    private:
        // Cache-line separation between producer and consumer state prevents
        // false sharing on SMP systems. The producer only writes m_head and
        // reads m_tail; the consumer only writes m_tail and reads m_head.
        alignas(64) Atomic<usize> m_head;
        alignas(64) Atomic<usize> m_tail;
        T m_slots[N];
    };

    // =========================================================================
    // DynamicRingBuffer<T, Alloc> — MPMC, spinlock-guarded, heap-backed
    //
    // Uses a SpinLock for mutual exclusion. This is NOT ISR-safe — do not call
    // Push or Pop from interrupt context. Use StaticRingBuffer / InterruptSafeQueue
    // for ISR→thread communication.
    // =========================================================================

    /// @brief Multi-producer / multi-consumer heap-backed ring buffer.
    ///
    /// @tparam T     Element type. Must be TriviallyCopyable.
    /// @tparam Alloc Allocator satisfying FoundationKitMemory::IAllocator.
    template <TriviallyCopyable T, FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class DynamicRingBuffer {
        using _check = TypeSanityCheck<T>;

    public:
        /// @brief Construct with a runtime capacity and an allocator instance.
        /// @param capacity Number of slots. Must be a non-zero power of two.
        explicit DynamicRingBuffer(usize capacity, Alloc alloc = {}) noexcept
            : m_alloc(Move(alloc)), m_capacity(capacity), m_head(0), m_tail(0), m_slots(nullptr)
        {
            FK_BUG_ON(capacity == 0, "DynamicRingBuffer: capacity must be > 0");
            FK_BUG_ON((capacity & (capacity - 1)) != 0,
                "DynamicRingBuffer: capacity ({}) must be a power of two", capacity);

            const auto res = m_alloc.Allocate(capacity * sizeof(T), alignof(T));
            FK_BUG_ON(!res.ok(), "DynamicRingBuffer: allocation of {} slots failed", capacity);
            m_slots = static_cast<T*>(res.ptr);
        }

        ~DynamicRingBuffer() noexcept {
            if (m_slots) m_alloc.Deallocate(m_slots, m_capacity * sizeof(T), alignof(T));
        }

        DynamicRingBuffer(const DynamicRingBuffer&)            = delete;
        DynamicRingBuffer& operator=(const DynamicRingBuffer&) = delete;

        /// @brief Push an element. Thread-safe, NOT ISR-safe.
        /// @return true if enqueued; false if full.
        [[nodiscard]] bool Push(const T& value) noexcept {
            LockGuard guard(m_lock);
            const usize next = (m_head + 1) & (m_capacity - 1);
            if (next == m_tail) return false; // full
            m_slots[m_head] = value;
            m_head = next;
            return true;
        }

        /// @brief Pop an element. Thread-safe, NOT ISR-safe.
        [[nodiscard]] Optional<T> Pop() noexcept {
            LockGuard guard(m_lock);
            if (m_tail == m_head) return {}; // empty
            T value = m_slots[m_tail];
            m_tail = (m_tail + 1) & (m_capacity - 1);
            return value;
        }

        [[nodiscard]] bool Empty() const noexcept {
            LockGuard guard(m_lock);
            return m_head == m_tail;
        }

        [[nodiscard]] usize Capacity() const noexcept { return m_capacity - 1; }

    private:
        Alloc              m_alloc;
        usize              m_capacity;
        usize              m_head;
        usize              m_tail;
        T*                 m_slots;
        mutable SpinLock   m_lock;
    };

} // namespace FoundationKitCxxStl::Structure
