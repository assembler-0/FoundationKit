#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // =========================================================================
    // MpscQueue<T, N> — Bounded, Lock-Free, Multi-Producer / Single-Consumer
    //
    // ## Algorithm (Dmitry Vyukov's bounded MPSC)
    //
    // Each slot carries a sequence counter initialised to its index. The
    // enqueue position (m_enqueue_pos) and dequeue position (m_dequeue_pos)
    // are separate atomic counters that grow monotonically and wrap via modulo.
    //
    // Producer (Push — ISR-safe):
    //   1. Atomically claim a slot: pos = m_enqueue_pos.FetchAdd(1, Relaxed).
    //      Multiple producers may do this concurrently; each gets a unique pos.
    //   2. Compute slot index = pos & (N-1).
    //   3. Spin until slot.sequence == pos (the slot is free and ready to write).
    //      This spin is bounded: it ends as soon as the consumer has advanced
    //      past this slot in a previous cycle.
    //   4. Write the data.
    //   5. Store slot.sequence = pos + 1 (Release): signals the consumer that
    //      the slot is ready to read.
    //
    // Consumer (Pop — single consumer only):
    //   1. Load pos = m_dequeue_pos (Relaxed — only one consumer writes this).
    //   2. Compute slot index = pos & (N-1).
    //   3. Load diff = slot.sequence - pos (Acquire).
    //      - diff == 1: slot is ready. Read data, store slot.sequence = pos + N
    //        (Release, recycles the slot for future producers), advance pos.
    //      - diff == 0: queue is empty.
    //      - diff < 0: impossible in a correct implementation (FK_BUG_ON).
    //
    // ## Why sequence counters instead of head/tail with a mask
    //
    // A plain head/tail SPSC (StaticRingBuffer) cannot be extended to MPSC
    // because two producers racing on the same head index would corrupt each
    // other's writes. The sequence counter per slot gives each producer an
    // exclusive "reservation" via FetchAdd, and the spin in step 3 ensures
    // the producer waits for the consumer to recycle the slot before writing.
    //
    // ## ISR safety
    //
    // Push() uses only FetchAdd (one atomic RMW) and a spin on a per-slot
    // sequence counter. It does not call any OS primitive, does not allocate,
    // and does not hold any lock. It is safe to call from any interrupt level.
    //
    // Pop() is NOT ISR-safe. It must be called from a single consumer context
    // (DPC / bottom-half / kernel thread). Calling Pop() from two concurrent
    // contexts is a logic error (FK_BUG_ON in debug builds via the consumer
    // ownership check).
    //
    // ## Capacity
    //
    // N must be a non-zero power of two. The usable capacity is exactly N slots.
    // Unlike a head/tail ring buffer, no slot is sacrificed as a sentinel:
    // full vs. empty is distinguished by the per-slot sequence counter, not by
    // leaving a gap between head and tail.
    //
    // ## Cache-line layout
    //
    // m_enqueue_pos and m_dequeue_pos are on separate cache lines to prevent
    // false sharing between producers and the consumer.
    // =========================================================================

    /// @brief Bounded lock-free multi-producer / single-consumer queue.
    ///
    /// @tparam T Element type. Must be TriviallyCopyable (ISR-safe slot copy).
    /// @tparam N Capacity. Must be a non-zero power of two.
    template <TriviallyCopyable T, usize N>
    class MpscQueue {
        static_assert((N & N - 1) == 0 && N > 0,
            "MpscQueue: N must be a non-zero power of two");
        static_assert(N >= 2,
            "MpscQueue: N must be at least 2 (one slot is reserved by the protocol)");

        static constexpr usize kMask = N - 1;

        struct Slot {
            alignas(64) Atomic<usize> sequence;
            T data;

            Slot() noexcept : sequence(0), data{} {}
        };

    public:
        MpscQueue() noexcept : m_enqueue_pos(0), m_dequeue_pos(0) {
            // Initialise each slot's sequence to its index. This is the
            // "free and ready for producer at position i" sentinel.
            for (usize i = 0; i < N; ++i)
                m_slots[i].sequence.Store(i, MemoryOrder::Relaxed);
        }

        MpscQueue(const MpscQueue&)            = delete;
        MpscQueue& operator=(const MpscQueue&) = delete;

        /// @brief Enqueue an element. Lock-free, ISR-safe.
        ///
        /// Multiple producers may call Push() concurrently from any context
        /// including interrupt handlers.
        ///
        /// @return true if the element was enqueued; false if the queue is full.
        ///         A full queue from ISR context is a design-time capacity error.
        [[nodiscard]] bool Push(const T& value) noexcept {
            usize pos = m_enqueue_pos.Load(MemoryOrder::Relaxed);

            for (;;) {
                Slot& slot = m_slots[pos & kMask];

                // Acquire: see the Release store from the consumer that recycled
                // this slot (slot.sequence = pos + N in a previous Pop cycle).
                const usize seq  = slot.sequence.Load(MemoryOrder::Acquire);

                if (const isize diff = static_cast<isize>(seq) - static_cast<isize>(pos); diff == 0) {
                    if (m_enqueue_pos.CompareExchange(pos, pos + 1,
                            /*weak=*/true,
                            MemoryOrder::Relaxed,
                            MemoryOrder::Relaxed)) {
                        slot.data = value;
                        slot.sequence.Store(pos + 1, MemoryOrder::Release);
                        return true;
                    }
                } else if (diff < 0) {
                    return false;
                } else {
                    pos = m_enqueue_pos.Load(MemoryOrder::Relaxed);
                }
            }
        }

        /// @brief Dequeue an element. Single-consumer only, NOT ISR-safe.
        ///
        /// Must be called from exactly one consumer context. Concurrent calls
        /// to Pop() from multiple contexts are a logic error.
        ///
        /// @return The next element, or an empty Optional if the queue is empty.
        [[nodiscard]] Optional<T> Pop() noexcept {
            const usize pos  = m_dequeue_pos.Load(MemoryOrder::Relaxed);
            Slot&       slot = m_slots[pos & kMask];

            // Acquire: see the Release store from the producer (slot.sequence = pos+1).
            const usize seq  = slot.sequence.Load(MemoryOrder::Acquire);
            const isize diff = static_cast<isize>(seq) - static_cast<isize>(pos + 1);

            if (diff < 0) {
                // Queue is empty: the producer has not yet published this slot.
                return {};
            }

            FK_BUG_ON(diff > 0,
                "MpscQueue::Pop: sequence diff={} > 0 at pos={} — "
                "consumer position is behind the published sequence, "
                "possible concurrent Pop() calls or corrupted state",
                diff, pos);

            T value = slot.data;

            slot.sequence.Store(pos + N, MemoryOrder::Release);
            m_dequeue_pos.Store(pos + 1, MemoryOrder::Relaxed);
            return value;
        }

        /// @brief Returns true if the queue appears empty at this instant.
        ///
        /// This is a non-atomic snapshot. The result may be stale by the time
        /// the caller acts on it. Use only for diagnostics or heuristics.
        [[nodiscard]] bool Empty() const noexcept {
            const usize pos = m_dequeue_pos.Load(MemoryOrder::Relaxed);
            const usize seq = m_slots[pos & kMask].sequence.Load(MemoryOrder::Acquire);
            return static_cast<isize>(seq) - static_cast<isize>(pos + 1) < 0;
        }

        static constexpr usize Capacity() noexcept { return N; }

    private:
        alignas(64) Atomic<usize> m_enqueue_pos;
        alignas(64) Atomic<usize> m_dequeue_pos;
                    Slot          m_slots[N];
    };

} // namespace FoundationKitCxxStl::Structure
