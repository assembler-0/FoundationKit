#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // SeqLock<T>
    //
    // ## Algorithm
    //
    // The sequence counter is an unsigned integer that is:
    //   - Even  when no write is in progress (data is consistent).
    //   - Odd   while a write is in progress (data is being modified).
    //
    // Writer protocol:
    //   1. Increment counter to odd  (Release: prevents reordering of the
    //      increment with the subsequent data writes).
    //   2. Write the data.
    //   3. Increment counter to even (Release: makes the data writes visible
    //      before the counter update that signals completion).
    //
    // Reader protocol:
    //   1. Load counter (Acquire: see the data writes that preceded the even
    //      counter store in step 3 of the writer).
    //   2. If odd, a write is in progress — spin.
    //   3. Snapshot the data with MemCpy (no copy constructor — T may not be
    //      in a consistent state mid-write; we just grab the bytes).
    //   4. Load counter again (Acquire).
    //   5. If counter changed, retry from step 1.
    //
    // ## Why MemCpy instead of assignment
    //
    // During a write, T's invariants may be temporarily violated. Invoking a
    // copy constructor or assignment operator on a partially-written T is
    // undefined behaviour. MemCpy copies the raw bytes without invoking any
    // user-defined logic, which is safe because T is TriviallyCopyable.
    //
    // ## Cache-line layout
    //
    // m_seq is on its own cache line. On a write, only m_seq bounces between
    // writer and readers; the data cache line is written once and then only
    // read. This avoids the "write invalidation storm" that a mutex would cause
    // by making readers take exclusive ownership of the lock cache line.
    //
    // ## Limitations
    //
    // - Writers must be serialized externally (SeqLock does not protect against
    //   concurrent writers). Use a SpinLock around Write() if needed.
    // - Not suitable for data containing pointers that are freed after update
    //   (use RCU for that). SeqLock is for value-type snapshots (timestamps,
    //   statistics, hardware register shadows).
    // =========================================================================

    /// @brief Sequence-lock for read-heavy, write-rare shared value types.
    ///
    /// @tparam T Must be TriviallyCopyable. sizeof(T) should be small (≤ one
    ///           cache line) for best performance; larger T increases retry cost.
    template <TriviallyCopyable T>
    class SeqLock {
        static_assert(sizeof(T) > 0, "SeqLock: T must be a non-empty type");

    public:
        /// @brief Construct with an initial value.
        explicit constexpr SeqLock(const T& initial) noexcept : m_seq(0) {
            CompilerBuiltins::MemCpy(&m_data, &initial, sizeof(T));
        }

        SeqLock(const SeqLock&)            = delete;
        SeqLock& operator=(const SeqLock&) = delete;

        /// @brief Write a new value.
        ///
        /// Writers MUST be serialized by the caller (e.g., with a SpinLock).
        /// Concurrent writers will corrupt both the sequence counter and the data.
        ///
        /// @param value The new value to publish.
        void Write(const T& value) noexcept {
            // Release: the data writes below must not be reordered before this
            // increment by the CPU or compiler.
            const usize seq = m_seq.Load(MemoryOrder::Relaxed);
            FK_BUG_ON((seq & 1u) != 0,
                "SeqLock::Write: sequence counter is odd ({}) — concurrent writers detected "
                "or previous Write() did not complete cleanly", seq);
            m_seq.Store(seq + 1, MemoryOrder::Release);

            CompilerBuiltins::MemCpy(&m_data, &value, sizeof(T));

            // Release: makes the data writes visible before the counter update.
            m_seq.Store(seq + 2, MemoryOrder::Release);
        }

        /// @brief Read a consistent snapshot of the value.
        ///
        /// Retries if a write is in progress or if the sequence counter changed
        /// during the read. In the common case (no concurrent writer), this is
        /// two Acquire loads and one MemCpy — no atomic RMW, no cache-line bounce.
        ///
        /// @return A consistent copy of the protected value.
        [[nodiscard]] T Read() const noexcept {
            T snapshot;
            usize seq0, seq1;
            do {
                // Acquire: see the data written before the even counter store.
                seq0 = m_seq.Load(MemoryOrder::Acquire);
                // Spin while a write is in progress (odd counter).
                // CpuPause() reduces power and avoids memory-bus saturation.
                while (seq0 & 1u) {
                    CompilerBuiltins::CpuPause();
                    seq0 = m_seq.Load(MemoryOrder::Acquire);
                }

                CompilerBuiltins::MemCpy(&snapshot, &m_data, sizeof(T));

                // Acquire: if seq1 == seq0, the MemCpy above saw a consistent
                // snapshot (no write started or completed during our read).
                seq1 = m_seq.Load(MemoryOrder::Acquire);
            } while (seq0 != seq1);

            return snapshot;
        }

        /// @brief Try to read without retrying.
        ///
        /// Returns false if a write was in progress or the counter changed.
        /// Useful in real-time contexts where spinning is unacceptable.
        ///
        /// @param out Receives the snapshot if the read was consistent.
        /// @return true if the snapshot is consistent; false if it should be discarded.
        [[nodiscard]] bool TryRead(T& out) const noexcept {
            const usize seq0 = m_seq.Load(MemoryOrder::Acquire);
            if (seq0 & 1u) return false; // write in progress

            CompilerBuiltins::MemCpy(&out, &m_data, sizeof(T));

            const usize seq1 = m_seq.Load(MemoryOrder::Acquire);
            return seq0 == seq1;
        }

        /// @brief Current sequence counter (even = consistent, odd = write in progress).
        [[nodiscard]] usize Sequence() const noexcept {
            return m_seq.Load(MemoryOrder::Acquire);
        }

    private:
        // m_seq on its own cache line: readers and writers only bounce this
        // line during the brief odd-sequence window, not the data line.
        alignas(64) Atomic<usize> m_seq;
                    T             m_data;
    };

} // namespace FoundationKitCxxStl::Sync
