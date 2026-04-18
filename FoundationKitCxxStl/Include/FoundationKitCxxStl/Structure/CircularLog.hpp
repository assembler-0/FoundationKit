#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Structure/RingBuffer.hpp>

namespace FoundationKitCxxStl::Structure {

    // =========================================================================
    // CircularLog<N> — lock-free SPSC diagnostic ring (flight recorder)
    //
    // Designed to be written from ANY context including NMI handlers.
    // Backed by StaticRingBuffer<LogEntry, N> which is already SPSC lock-free
    // with correct acquire/release ordering.
    //
    // Each entry is a fixed-size char buffer (kEntrySize bytes) so the type
    // is TriviallyCopyable — a hard requirement for StaticRingBuffer and for
    // safe NMI-context writes (no heap, no locks, no destructors).
    //
    // N must be a non-zero power of two (StaticRingBuffer requirement).
    //
    // Log format: entries are null-terminated C strings truncated to kEntrySize-1.
    // The debugger / serial console reads them via Drain() or ForEach().
    //
    // Thread-safety model (inherited from StaticRingBuffer):
    //   - ONE producer (any context, including NMI).
    //   - ONE consumer (thread context only).
    //   If MPSC is needed, wrap the producer side with a SpinLock.
    // =========================================================================

    /// @brief Fixed-size log entry. TriviallyCopyable so it can live in StaticRingBuffer.
    struct LogEntry {
        static constexpr usize kEntrySize = 128;
        char text[kEntrySize];
    };
    static_assert(TriviallyCopyable<LogEntry>,
        "CircularLog: LogEntry must be trivially copyable for NMI-safe writes");

    /// @brief Lock-free SPSC diagnostic ring buffer (flight recorder).
    /// @tparam N Capacity in entries. Must be a non-zero power of two.
    template <usize N>
    class CircularLog {
        static_assert((N & (N - 1)) == 0 && N > 0,
            "CircularLog: N must be a non-zero power of two");
    public:
        CircularLog() noexcept = default;

        CircularLog(const CircularLog&)            = delete;
        CircularLog& operator=(const CircularLog&) = delete;

        /// @brief Write a null-terminated string to the log.
        ///        Safe from any context including NMI. Truncates to kEntrySize-1.
        ///        Returns false if the ring is full (entry is dropped).
        ///
        /// @param msg  Null-terminated message string.
        [[nodiscard]] bool Write(const char* msg) noexcept {
            FK_BUG_ON(msg == nullptr, "CircularLog::Write: msg must not be null");
            LogEntry entry{};
            // Manual bounded copy — no libc, no exceptions.
            usize i = 0;
            while (i < LogEntry::kEntrySize - 1 && msg[i] != '\0') {
                entry.text[i] = msg[i];
                ++i;
            }
            entry.text[i] = '\0';
            return m_ring.Push(entry);
        }

        /// @brief Write a pre-formatted LogEntry directly (zero-copy path for
        ///        callers that already have a StaticStringBuilder buffer).
        [[nodiscard]] bool WriteEntry(const LogEntry& entry) noexcept {
            return m_ring.Push(entry);
        }

        /// @brief Pop and process all available entries in FIFO order.
        ///        Callable from thread context only (consumer side).
        ///
        /// @param fn  Callable with signature `void(const LogEntry&)`.
        template <Invocable<const LogEntry&> Fn>
        void Drain(Fn&& fn) noexcept {
            while (auto entry = m_ring.Pop()) {
                fn(*entry);
            }
        }

        /// @brief Peek at all available entries without consuming them.
        ///        NOTE: This is NOT logically const — it drains the ring into a
        ///        staging array and re-pushes entries. Entries that do not fit
        ///        back in (ring full due to concurrent producer) are lost.
        ///        Intended for debugger/serial dump only; do not call on hot paths.
        ///
        /// @param fn  Callable with signature `void(const LogEntry&)`.
        template <Invocable<const LogEntry&> Fn>
        void DrainAndRestore(Fn&& fn) noexcept {
            LogEntry staging[N];
            usize count = 0;
            while (count < N) {
                auto e = m_ring.Pop();
                if (!e) break;
                staging[count++] = *e;
            }
            for (usize i = 0; i < count; ++i) fn(staging[i]);
            for (usize i = 0; i < count; ++i) m_ring.Push(staging[i]);
        }

        [[nodiscard]] bool Empty() const noexcept { return m_ring.Empty(); }
        [[nodiscard]] usize Size()  const noexcept { return m_ring.Size(); }
        static constexpr usize Capacity() noexcept { return StaticRingBuffer<LogEntry, N>::Capacity(); }

    private:
        StaticRingBuffer<LogEntry, N> m_ring;
    };

} // namespace FoundationKitCxxStl::Structure
