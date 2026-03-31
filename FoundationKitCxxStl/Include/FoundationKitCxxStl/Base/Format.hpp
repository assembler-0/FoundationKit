#pragma once

#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    class StringBuilder;

    template <typename T>
    struct Formatter {
        void Format(StringBuilder& sb, const T& value);
    };

    namespace Detail {
        template <Unsigned T>
        constexpr usize UnsignedToChars(T value, char* buffer, u32 base = 10, bool uppercase = false) {
            if (value == 0) {
                buffer[0] = '0';
                return 1;
            }

            const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
            usize i = 0;
            while (value > 0) {
                buffer[i++] = digits[value % base];
                value /= base;
            }

            for (usize j = 0; j < i / 2; ++j) {
                Swap(buffer[j], buffer[i - j - 1]);
            }

            return i;
        }

        template <Signed T>
        constexpr usize SignedToChars(T value, char* buffer, u32 base = 10, bool uppercase = false) {
            if (value == 0) {
                buffer[0] = '0';
                return 1;
            }

            usize i = 0;
            bool negative = false;
            if (value < 0 && base == 10) {
                negative = true;
                MakeUnsignedT<T> uval = static_cast<MakeUnsignedT<T>>(-(value + 1)) + 1;
                i = UnsignedToChars(uval, buffer, base, uppercase);
            } else {
                i = UnsignedToChars(static_cast<MakeUnsignedT<T>>(value), buffer, base, uppercase);
                return i; // already handled by UnsignedToChars
            }

            // If negative, we need to shift and add '-'
            if (negative) {
                for (usize j = i; j > 0; --j) {
                    buffer[j] = buffer[j - 1];
                }
                buffer[0] = '-';
                i++;
            }

            return i;
        }
    }

} // namespace FoundationKitCxxStl
