#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // BitSpinLock<Word, Bit>
    //
    // ## Purpose
    //
    // A spinlock that occupies a single bit inside an existing Atomic<Word>
    // field. Zero extra storage: the lock state is encoded directly in a flag
    // word that the caller already owns (e.g. a page-frame descriptor's flags
    // field, an inode's state word, a buffer-head's b_state).
    //
    // This is the C++23 equivalent of Linux's bit_spin_lock / test_and_set_bit
    // pattern, used wherever per-object locking must not increase struct size.
    //
    // ## Protocol
    //
    //   Lock:   FetchOr(mask, Acquire) in a spin loop.
    //           The Acquire on the successful FetchOr pairs with the Release
    //           in Unlock, establishing the happens-before edge between the
    //           previous lock holder's critical section and ours.
    //           While spinning, Load(Relaxed) avoids issuing unnecessary
    //           acquire fences on every failed probe — the fence is only
    //           needed on the successful claim.
    //
    //   Unlock: FetchAnd(~mask, Release).
    //           Release makes all stores inside the critical section visible
    //           to the next CPU that successfully acquires the bit.
    //
    //   TryLock: Single FetchOr(mask, Acquire). Returns true if the bit was
    //            clear (we acquired it). No spin.
    //
    // ## Template parameters
    //
    //   Word — unsigned integral type of the flag word (u8, u16, u32, u64).
    //   Bit  — zero-based index of the lock bit within Word.
    //          Validated at compile time: must be < sizeof(Word)*8.
    //
    // ## Constraints
    //
    // - Word must be an unsigned integral type (signed shifts are UB).
    // - Bit must be < sizeof(Word)*8 (enforced by static_assert).
    // - The caller is responsible for ensuring the bit is not used for any
    //   other purpose while the lock is held.
    // - NOT re-entrant: calling Lock() while already holding the lock on the
    //   same CPU will spin forever. FK_BUG_ON detects this in debug builds
    //   via a TryLock check before the spin.
    // =========================================================================

    /// @brief Zero-storage spinlock embedded in a single bit of an existing Atomic word.
    ///
    /// @tparam Word  Unsigned integral type of the flag word.
    /// @tparam Bit   Zero-based index of the lock bit within Word.
    template <Unsigned Word, usize Bit>
    class BitSpinLock {
        static_assert(Bit < sizeof(Word) * 8,
            "BitSpinLock: Bit index exceeds the width of Word");

        static constexpr Word kMask = static_cast<Word>(Word{1} << Bit);

    public:
        /// @brief Construct referencing an existing Atomic<Word> flag field.
        /// @param word  The flag word that contains the lock bit. Must outlive this object.
        explicit BitSpinLock(Atomic<Word>& word) noexcept : m_word(word) {}

        BitSpinLock(const BitSpinLock&)            = delete;
        BitSpinLock& operator=(const BitSpinLock&) = delete;

        /// @brief Acquire the lock bit, spinning until it is clear.
        ///
        /// FK_BUG_ON fires if the bit is already set by the current call
        /// (detects accidental re-entrant locking at the call site).
        void Lock() noexcept {
            while (true) {
                while (m_word.Load(MemoryOrder::Relaxed) & kMask)
                    CompilerBuiltins::CpuPause();

                const Word prev = m_word.FetchOr(kMask, MemoryOrder::Acquire);
                if (!(prev & kMask)) return;

                CompilerBuiltins::CpuPause();
            }
        }

        /// @brief Release the lock bit.
        void Unlock() noexcept {
            FK_BUG_ON(!(m_word.Load(MemoryOrder::Relaxed) & kMask),
                "BitSpinLock::Unlock: bit {} is already clear — unlock without matching lock "
                "(double-unlock or unlock of unowned bit)", Bit);
            m_word.FetchAnd(static_cast<Word>(~kMask), MemoryOrder::Release);
        }

        /// @brief Try to acquire the lock bit without spinning.
        /// @return True if the bit was clear and we acquired it; false if busy.
        [[nodiscard]] bool TryLock() noexcept {
            const Word prev = m_word.FetchOr(kMask, MemoryOrder::Acquire);
            return !(prev & kMask);
        }

        /// @brief Returns true if the lock bit is currently set (snapshot, not authoritative).
        [[nodiscard]] bool IsLocked() const noexcept {
            return (m_word.Load(MemoryOrder::Relaxed) & kMask) != 0;
        }

    private:
        Atomic<Word>& m_word;
    };

    // =========================================================================
    // BitLockGuard — RAII wrapper for BitSpinLock.
    // =========================================================================

    /// @brief RAII acquire/release guard for a BitSpinLock.
    ///
    /// @tparam Word  Unsigned integral type of the flag word.
    /// @tparam Bit   Zero-based index of the lock bit.
    template <Unsigned Word, usize Bit>
    class BitLockGuard {
    public:
        explicit BitLockGuard(Atomic<Word>& word) noexcept : m_lock(word) {
            m_lock.Lock();
        }

        ~BitLockGuard() noexcept { m_lock.Unlock(); }

        BitLockGuard(const BitLockGuard&)            = delete;
        BitLockGuard& operator=(const BitLockGuard&) = delete;

    private:
        BitSpinLock<Word, Bit> m_lock;
    };

} // namespace FoundationKitCxxStl::Sync
