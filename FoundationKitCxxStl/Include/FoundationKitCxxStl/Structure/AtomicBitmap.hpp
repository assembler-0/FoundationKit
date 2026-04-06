#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Bit.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl::Sync;

    // =========================================================================
    // AtomicBitmap<N> — lock-free bit allocator
    //
    // Each word is an Atomic<usize>.  Set/Reset use FetchOr/FetchAnd with
    // AcqRel ordering so that the caller sees a consistent view of the bit
    // after the operation.
    //
    // FindFirstUnsetAndSet uses a per-word CAS loop:
    //   1. Load the word (Acquire).
    //   2. Invert to find zero bits, mask the last word to N % kWordBits.
    //   3. CountTrailingZeros (via Bit.hpp) to find the lowest free bit.
    //   4. CAS to atomically claim that bit.
    //   5. On CAS failure current is refreshed — retry the same word.
    //
    // Memory ordering rationale:
    //   - FetchOr/FetchAnd AcqRel: the caller that sets a bit "owns" the
    //     resource; AcqRel ensures prior writes are visible to any CPU that
    //     subsequently observes the bit as set.
    //   - FindFirstUnsetAndSet CAS success=AcqRel / failure=Acquire: the
    //     successful CAS acquires ownership; the failure path re-reads the
    //     word with Acquire without a full fence.
    // =========================================================================

    /// @brief Lock-free bitmap with atomic Set, Reset, and FindFirstUnsetAndSet.
    /// @tparam N Number of bits. Must be greater than zero.
    template <usize N>
    class AtomicBitmap {
        static_assert(N > 0, "AtomicBitmap: N must be greater than zero");

    public:
        static constexpr usize kWordBits  = sizeof(usize) * 8;
        static constexpr usize kWordCount = (N + kWordBits - 1) / kWordBits;

        AtomicBitmap() noexcept {
            for (usize i = 0; i < kWordCount; ++i)
                m_words[i].Store(0, MemoryOrder::Relaxed);
        }

        AtomicBitmap(const AtomicBitmap&)            = delete;
        AtomicBitmap& operator=(const AtomicBitmap&) = delete;

        /// @brief Atomically set bit `pos`.
        void Set(usize pos) noexcept {
            FK_BUG_ON(pos >= N, "AtomicBitmap::Set: bit ({}) out of range ({})", pos, N);
            m_words[pos / kWordBits].FetchOr(
                static_cast<usize>(1) << (pos % kWordBits),
                MemoryOrder::AcqRel);
        }

        /// @brief Atomically clear bit `pos`.
        void Reset(usize pos) noexcept {
            FK_BUG_ON(pos >= N, "AtomicBitmap::Reset: bit ({}) out of range ({})", pos, N);
            m_words[pos / kWordBits].FetchAnd(
                ~(static_cast<usize>(1) << (pos % kWordBits)),
                MemoryOrder::AcqRel);
        }

        /// @brief Snapshot test of bit `pos` (Acquire load, not a CAS).
        [[nodiscard]] bool Test(usize pos) const noexcept {
            FK_BUG_ON(pos >= N, "AtomicBitmap::Test: bit ({}) out of range ({})", pos, N);
            const usize word = m_words[pos / kWordBits].Load(MemoryOrder::Acquire);
            return (word >> (pos % kWordBits)) & usize{1};
        }

        /// @brief Atomically find the first unset bit and set it (allocate).
        /// @returns The allocated bit index, or N if all bits are set.
        [[nodiscard]] usize FindFirstUnsetAndSet() noexcept {
            for (usize w = 0; w < kWordCount; ++w) {
                const usize valid_mask = (w == kWordCount - 1 && (N % kWordBits) != 0)
                    ? (static_cast<usize>(1) << (N % kWordBits)) - 1
                    : static_cast<usize>(-1);

                usize current = m_words[w].Load(MemoryOrder::Acquire);
                while (true) {
                    const usize free_bits = ~current & valid_mask;
                    if (free_bits == 0) break; // word fully allocated, try next

                    const auto bit_in_word = static_cast<usize>(
                        CountTrailingZeros(free_bits));
                    const usize desired = current | (static_cast<usize>(1) << bit_in_word);

                    // Weak CAS: on failure `current` is refreshed automatically.
                    if (m_words[w].CompareExchange(current, desired,
                            true,
                            MemoryOrder::AcqRel,
                            MemoryOrder::Acquire)) {
                        return w * kWordBits + bit_in_word;
                    }
                }
            }
            return N; // all bits set
        }

        /// @brief Returns the number of set bits (non-atomic snapshot).
        [[nodiscard]] usize Count() const noexcept {
            usize total = 0;
            for (usize i = 0; i < kWordCount; ++i)
                total += static_cast<usize>(PopCount(m_words[i].Load(MemoryOrder::Relaxed)));
            return total;
        }

        static constexpr usize Size() noexcept { return N; }

    private:
        Atomic<usize> m_words[kWordCount];
    };

} // namespace FoundationKitCxxStl::Structure
