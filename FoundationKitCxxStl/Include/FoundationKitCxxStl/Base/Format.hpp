#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    /// @brief Formatting specification for advanced layout control.
    struct FormatSpec {
        usize width      = 0;     // Minimum field width
        u8    fill       = ' ';   // Padding character
        u8    base       = 10;    // Number base (2, 8, 10, 16)
        u8    precision  = 6;     // Decimal precision (for floats)
        bool  uppercase  = false; // Use uppercase for hex/base-36
        bool  prefix     = false; // Show base prefix (0x, 0b)
        bool  align_left = false; // Alignment direction
        bool  zero_pad   = false; // Short-hand for fill='0'
    };

    template <typename T>
    struct Formatter {
        /// @brief Format a value into a sink with optional specification.
        template <typename Sink>
        void Format(Sink& sb, const T& value, const FormatSpec& spec = {});
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
                // Avoid overflow on Min value by using unsigned math
                MakeUnsignedT<T> uval = static_cast<MakeUnsignedT<T>>(-(value + 1)) + 1;
                i = UnsignedToChars(uval, buffer, base, uppercase);
            } else {
                i = UnsignedToChars(static_cast<MakeUnsignedT<T>>(value), buffer, base, uppercase);
                return i;
            }

            if (negative) {
                for (usize j = i; j > 0; --j) buffer[j] = buffer[j - 1];
                buffer[0] = '-';
                i++;
            }

            return i;
        }

        /// @brief Pad a string in a sink based on FormatSpec.
        template <typename Sink>
        void Pad(Sink& sb, usize content_len, const FormatSpec& spec) {
            if (spec.width > content_len) {
                usize padding = spec.width - content_len;
                char f = spec.zero_pad ? '0' : spec.fill;
                while (padding--) sb.Append(f);
            }
        }
    }

    // --- Basic Specializations ---

    template <>
    struct Formatter<char> {
        template <typename Sink>
        void Format(Sink& sb, const char& value, const FormatSpec& spec = {}) {
            if (!spec.align_left) Detail::Pad(sb, 1, spec);
            sb.Append(value);
            if (spec.align_left) Detail::Pad(sb, 1, spec);
        }
    };

    template <>
    struct Formatter<const char*> {
        template <typename Sink>
        void Format(Sink& sb, const char* const& value, const FormatSpec& spec = {}) {
            if (!value) {
                if (!spec.align_left) Detail::Pad(sb, 6, spec);
                sb.Append("(null)", 6);
                if (spec.align_left) Detail::Pad(sb, 6, spec);
                return;
            }
            usize len = 0;
            while (value[len]) len++;
            if (!spec.align_left) Detail::Pad(sb, len, spec);
            sb.Append(value, len);
            if (spec.align_left) Detail::Pad(sb, len, spec);
        }
    };

    template <usize N>
    struct Formatter<char[N]> : Formatter<const char*> {};

    template <usize N>
    struct Formatter<const char[N]> : Formatter<const char*> {};

    template <>
    struct Formatter<bool> {
        template <typename Sink>
        void Format(Sink& sb, const bool& value, const FormatSpec& spec = {}) {
            const char* s = value ? "true" : "false";
            usize len = value ? 4 : 5;
            if (!spec.align_left) Detail::Pad(sb, len, spec);
            sb.Append(s, len);
            if (spec.align_left) Detail::Pad(sb, len, spec);
        }
    };

    template <Integral T>
    struct Formatter<T> {
        template <typename Sink>
        void Format(Sink& sb, const T& value, const FormatSpec& spec = {}) {
            char buf[128]; 

            if (spec.prefix) {
                if (spec.base == 16) { sb.Append('0'); sb.Append(spec.uppercase ? 'X' : 'x'); }
                else if (spec.base == 2) { sb.Append('0'); sb.Append('b'); }
                else if (spec.base == 8) { sb.Append('0'); }
            }

            usize len;
            if constexpr (Signed<T>) len = Detail::SignedToChars(value, buf, spec.base, spec.uppercase);
            else len = Detail::UnsignedToChars(value, buf, spec.base, spec.uppercase);
            
            if (!spec.align_left) Detail::Pad(sb, len, spec);
            sb.Append(buf, len);
            if (spec.align_left) Detail::Pad(sb, len, spec);
        }
    };

    template <Pointer T>
    struct Formatter<T> {
        template <typename Sink>
        void Format(Sink& sb, const T& value, const FormatSpec& spec = {}) {
            if (!value) {
                if (!spec.align_left) Detail::Pad(sb, 4, spec);
                sb.Append("null", 4);
                if (spec.align_left) Detail::Pad(sb, 4, spec);
                return;
            }
            FormatSpec ptr_spec = spec;
            ptr_spec.base = 16;
            ptr_spec.uppercase = true;
            ptr_spec.prefix = true;
            Formatter<uptr>().Format(sb, reinterpret_cast<uptr>(value), ptr_spec);
        }
    };

    template <FloatingPoint T>
    struct Formatter<T> {
        template <typename Sink>
        void Format(Sink& sb, const T& value, const FormatSpec& spec = {}) {
             // Basic naive float implementation for kernel-safe logging
             T val = value;
             if (val < 0) { sb.Append('-'); val = -val; }

             u64 integral = static_cast<u64>(val);
             Formatter<u64>().Format(sb, integral);
             sb.Append('.');

             T fraction = val - static_cast<T>(integral);
             for (u8 i = 0; i < spec.precision; ++i) {
                 fraction *= 10;
                 u8 digit = static_cast<u8>(fraction);
                 sb.Append(static_cast<char>('0' + digit));
                 fraction -= digit;
             }
        }
    };

    // --- Range Formatting ---
    template <Range R>
    struct Formatter<R> {
        template <typename Sink>
        void Format(Sink& sb, const R& range, const FormatSpec& spec = {}) {
            sb.Append('[');
            bool first = true;
            for (auto&& item : range) {
                if (!first) sb.Append(", ", 2);
                Formatter<Unqualified<decltype(item)>>().Format(sb, item, spec);
                first = false;
            }
            sb.Append(']');
        }
    };

} // namespace FoundationKitCxxStl
