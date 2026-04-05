#pragma once

#include <FoundationKitCxxStl/Structure/RingBuffer.hpp>

namespace FoundationKitCxxStl::Structure {

    // =========================================================================
    // InterruptSafeQueue<T, N>
    //
    // A single-producer (ISR) / single-consumer (thread) queue that makes the
    // ISR/thread boundary explicit in the API. It is a thin, named wrapper over
    // StaticRingBuffer with two intentionally asymmetric entry points:
    //
    //   PushFromIsr   — callable from interrupt context.
    //                   No locks. No allocation. No OS calls. Lock-free.
    //   PopFromThread — callable from thread context only.
    //                   May be called with interrupts enabled or disabled.
    //
    // The WorkQueue in Structure/WorkQueue.hpp uses a Mutex and ConditionVariable
    // and is therefore ISR-unsafe. This type replaces it for interrupt→thread
    // message passing (e.g. device driver RX rings, timer event queues).
    //
    // N must be a non-zero power of two (inherited constraint from StaticRingBuffer).
    // =========================================================================

    /// @brief ISR-safe producer, thread-safe consumer queue.
    ///
    /// @tparam T  Element type. Must be TriviallyCopyable.
    /// @tparam N  Capacity (power of two).
    template <TriviallyCopyable T, usize N>
    class InterruptSafeQueue {
    public:
        InterruptSafeQueue() noexcept = default;

        InterruptSafeQueue(const InterruptSafeQueue&)            = delete;
        InterruptSafeQueue& operator=(const InterruptSafeQueue&) = delete;

        /// @brief Enqueue an element from interrupt context.
        ///
        /// This function is lock-free and does not call any OS primitives.
        /// It is safe to call from any interrupt priority level.
        ///
        /// @return true if the element was enqueued; false if the queue is full.
        ///         A full queue from ISR context is a design-time capacity error —
        ///         the consumer thread is not draining fast enough.
        [[nodiscard]] bool PushFromIsr(const T& value) noexcept {
            return m_ring.Push(value);
        }

        /// @brief Dequeue an element from thread context.
        ///
        /// Must NOT be called from interrupt context. If you need to drain the
        /// queue from an ISR, reconsider your architecture — use a deferred
        /// work mechanism (e.g. a bottom-half / tasklet pattern).
        ///
        /// @return The next element, or an empty Optional if the queue is empty.
        [[nodiscard]] Optional<T> PopFromThread() noexcept {
            return m_ring.Pop();
        }

        [[nodiscard]] bool Empty() const noexcept { return m_ring.Empty(); }
        [[nodiscard]] usize Size()  const noexcept { return m_ring.Size(); }
        static constexpr usize Capacity() noexcept { return StaticRingBuffer<T, N>::Capacity(); }

    private:
        StaticRingBuffer<T, N> m_ring;
    };

} // namespace FoundationKitCxxStl::Structure
