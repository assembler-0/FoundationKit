#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Bit.hpp>
#include <FoundationKit/Memory/Operations.hpp>

namespace FoundationKit::Structure {

    /// @brief A fixed-size sequence of bits.
    /// @tparam N Number of bits.
    template <usize N>
    class BitSet {
    public:
        static constexpr usize WordSize = sizeof(usize) * 8;
        static constexpr usize WordCount = (N + WordSize - 1) / WordSize;

        constexpr BitSet() noexcept {
            Memory::MemoryZero(m_words, sizeof(m_words));
        }

        constexpr void Set(const usize pos, const bool value = true) noexcept {
            if (pos >= N) return;
            if (value) m_words[pos / WordSize] |= static_cast<usize>(1) << (pos % WordSize);
            else Reset(pos);
        }

        constexpr void Reset(const usize pos) noexcept {
            if (pos >= N) return;
            m_words[pos / WordSize] &= ~(static_cast<usize>(1) << (pos % WordSize));
        }

        constexpr void Reset() noexcept {
            Memory::MemoryZero(m_words, sizeof(m_words));
        }

        constexpr void Flip(const usize pos) noexcept {
            if (pos >= N) return;
            m_words[pos / WordSize] ^= static_cast<usize>(1) << (pos % WordSize);
        }

        [[nodiscard]] constexpr bool Test(const usize pos) const noexcept {
            if (pos >= N) return false;
            return (m_words[pos / WordSize] & static_cast<usize>(1) << (pos % WordSize)) != 0;
        }

        [[nodiscard]] constexpr bool All() const noexcept {
            for (usize i = 0; i < WordCount - 1; ++i) {
                if (m_words[i] != static_cast<usize>(-1)) return false;
            }
            // Check last word carefully
            const usize remaining = N % WordSize;
            if (remaining == 0) return m_words[WordCount - 1] == static_cast<usize>(-1);
            const usize mask = (static_cast<usize>(1) << remaining) - 1;
            return (m_words[WordCount - 1] & mask) == mask;
        }

        [[nodiscard]] constexpr bool Any() const noexcept {
            for (usize i = 0; i < WordCount; ++i) {
                if (m_words[i] != 0) return true;
            }
            return false;
        }

        [[nodiscard]] constexpr bool None() const noexcept {
            return !Any();
        }

        [[nodiscard]] constexpr usize Count() const noexcept {
            usize total = 0;
            for (usize i = 0; i < WordCount; ++i) {
                total += static_cast<usize>(PopCount(m_words[i]));
            }
            return total;
        }

        [[nodiscard]] static constexpr usize Size() noexcept {
            return N;
        }

        /// @brief Find the index of the first bit that is set.
        /// @return Index or N if not found.
        [[nodiscard]] constexpr usize FindFirstSet() const noexcept {
            for (usize i = 0; i < WordCount; ++i) {
                if (m_words[i] != 0) {
                    return i * WordSize + static_cast<usize>(CountTrailingZeros(m_words[i]));
                }
            }
            return N;
        }

        /// @brief Find the index of the first bit that is NOT set.
        /// @return Index or N if not found.
        [[nodiscard]] constexpr usize FindFirstUnset() const noexcept {
            for (usize i = 0; i < WordCount; ++i) {
                usize inverted = ~m_words[i];
                if (i == WordCount - 1 && N % WordSize != 0) {
                    inverted &= (static_cast<usize>(1) << (N % WordSize)) - 1;
                }
                if (inverted != 0) {
                    return i * WordSize + static_cast<usize>(CountTrailingZeros(inverted));
                }
            }
            return N;
        }

    private:
        usize m_words[WordCount]{};
    };

} // namespace FoundationKit::Structure
