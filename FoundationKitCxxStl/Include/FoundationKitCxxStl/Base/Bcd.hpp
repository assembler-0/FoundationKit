#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitCxxStl::Base {

    /// @brief Binary Coded Decimal (BCD) utility.
    /// @desc Essential for interacting with legacy hardware (RTC, CMOS) and
    ///       certain industrial protocols.
    class Bcd {
    public:
        /// @brief Convert a byte from BCD to binary.
        /// @example 0x12 -> 12
        [[nodiscard]] static constexpr u8 ToBinary(u8 bcd) noexcept {
            return ((bcd >> 4) * 10) + (bcd & 0x0F);
        }

        /// @brief Convert a byte from binary to BCD.
        /// @example 12 -> 0x12
        [[nodiscard]] static constexpr u8 FromBinary(u8 bin) noexcept {
            return ((bin / 10) << 4) | (bin % 10);
        }

        /// @brief Validates if a byte is a valid BCD value (no nibble > 9).
        [[nodiscard]] static constexpr bool IsValid(u8 bcd) noexcept {
            return (bcd & 0x0F) <= 9 && (bcd >> 4) <= 9;
        }
    };

} // namespace FoundationKitCxxStl::Base
