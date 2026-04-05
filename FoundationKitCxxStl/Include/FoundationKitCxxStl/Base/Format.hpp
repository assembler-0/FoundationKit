#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    enum class Align : u8 {
        None,
        Left,
        Right,
        Center
    };

    enum class Sign : u8 {
        None,
        Plus,
        Minus,
        Space
    };

    /// @brief Formatting specification for advanced layout control.
    struct FormatSpec {
        usize width      = 0;     // Minimum field width
        u8    fill       = ' ';   // Padding character
        u8    base       = 10;    // Number base (2, 8, 10, 16)
        u8    precision  = 6;     // Decimal precision (for floats)
        Align align      = Align::None;
        Sign  sign       = Sign::None;
        bool  uppercase  = false; // Use uppercase for hex/base-36
        bool  prefix     = false; // Show base prefix (0x, 0b)
        bool  alternate  = false; // Alternate form (#)
        bool  zero_pad   = false; // Short-hand for fill='0'
        char  type       = '\0';  // Format type (d, x, f, s, ...)
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
            if (value < 0 && (base == 10 || base == 0)) { // base 0 means default for type
                negative = true;
                // Avoid overflow on Min value by using unsigned math
                MakeUnsignedT<T> uval = static_cast<MakeUnsignedT<T>>(-(value + 1)) + 1;
                i = UnsignedToChars(uval, buffer, base == 0 ? 10 : base, uppercase);
            } else {
                i = UnsignedToChars(static_cast<MakeUnsignedT<T>>(value), buffer, base == 0 ? 10 : base, uppercase);
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
                
                if (spec.align == Align::Center) {
                    usize left_padding = padding / 2;
                    for (usize i = 0; i < left_padding; ++i) sb.Append(f);
                } else if (spec.align == Align::Right || (spec.align == Align::None && !spec.zero_pad)) {
                    // Default for numbers is right align, but for strings it's left.
                    // This Pad function is called twice (before and after content).
                    // We need a better way to handle different alignments.
                }
            }
        }

        /// @brief Generic padding handler
        template <typename Sink, typename F>
        void WriteWithPadding(Sink& sb, usize content_len, const FormatSpec& spec, F&& write_content) {
            if (spec.width <= content_len) {
                write_content();
                return;
            }

            usize padding = spec.width - content_len;
            char f = spec.zero_pad ? '0' : spec.fill;
            Align align = spec.align;
            
            // Default alignment: Right for numbers, Left for others? 
            // std::format: 
            // - left for most types
            // - right for numbers
            // - none (usually same as right for numbers, left for others)
            
            if (align == Align::None) {
                // We'll let specializations decide or default to Left for strings, Right for numbers
                align = Align::Left; 
            }

            if (align == Align::Left) {
                write_content();
                for (usize i = 0; i < padding; ++i) sb.Append(f);
            } else if (align == Align::Right) {
                for (usize i = 0; i < padding; ++i) sb.Append(f);
                write_content();
            } else if (align == Align::Center) {
                usize left_padding = padding / 2;
                usize right_padding = padding - left_padding;
                for (usize i = 0; i < left_padding; ++i) sb.Append(f);
                write_content();
                for (usize i = 0; i < right_padding; ++i) sb.Append(f);
            }
        }

        /// @brief Parse FormatSpec from a string view.
        /// format-spec ::= [[fill]align][sign]["#"]["0"][width]["." precision][type]
        constexpr const char* ParseSpec(const char* begin, const char* end, FormatSpec& spec) {
            if (begin == end) return begin;

            const char* curr = begin;

            // [fill]align
            if (curr + 1 < end && (curr[1] == '<' || curr[1] == '>' || curr[1] == '^')) {
                spec.fill = static_cast<u8>(*curr);
                curr++;
            }
            if (curr < end) {
                if (*curr == '<') { spec.align = Align::Left; curr++; }
                else if (*curr == '>') { spec.align = Align::Right; curr++; }
                else if (*curr == '^') { spec.align = Align::Center; curr++; }
            }

            // sign
            if (curr < end) {
                if (*curr == '+') { spec.sign = Sign::Plus; curr++; }
                else if (*curr == '-') { spec.sign = Sign::Minus; curr++; }
                else if (*curr == ' ') { spec.sign = Sign::Space; curr++; }
            }

            // # (alternate form)
            if (curr < end && *curr == '#') {
                spec.alternate = true;
                spec.prefix = true;
                curr++;
            }

            // 0 (zero padding)
            if (curr < end && *curr == '0') {
                spec.zero_pad = true;
                curr++;
            }

            // width
            if (curr < end && *curr >= '0' && *curr <= '9') {
                usize w = 0;
                while (curr < end && *curr >= '0' && *curr <= '9') {
                    w = w * 10 + (*curr - '0');
                    curr++;
                }
                spec.width = w;
            }

            // .precision
            if (curr < end && *curr == '.') {
                curr++;
                usize p = 0;
                while (curr < end && *curr >= '0' && *curr <= '9') {
                    p = p * 10 + (*curr - '0');
                    curr++;
                }
                spec.precision = static_cast<u8>(p);
            }

            // type
            if (curr < end) {
                char t = *curr;
                if ((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z')) {
                    spec.type = t;
                    if (t == 'X') spec.uppercase = true;
                    if (t == 'x' || t == 'X') spec.base = 16;
                    else if (t == 'b' || t == 'B') { spec.base = 2; if (t == 'B') spec.uppercase = true; }
                    else if (t == 'o') spec.base = 8;
                    curr++;
                }
            }

            return curr;
        }
    }


    template <typename T>
    struct Formatter {
        /// @brief Format a value into a sink with optional specification.
        template <typename Sink>
        void Format(Sink& sb, const T& /*value*/, const FormatSpec& spec = {}) const noexcept {
            const char str[] = "[Object]";
            Detail::WriteWithPadding(sb, 8, spec, [&] {
                sb.Append(str, 8);
            });
        }
    };


    // --- Basic Specializations ---

    template <>
    struct Formatter<char> {
        template <typename Sink>
        void Format(Sink& sb, const char& value, const FormatSpec& spec = {}) {
            Detail::WriteWithPadding(sb, 1, spec, [&] {
                sb.Append(value);
            });
        }
    };

    template <>
    struct Formatter<const char*> {
        template <typename Sink>
        void Format(Sink& sb, const char* const& value, const FormatSpec& spec = {}) {
            if (!value) {
                Detail::WriteWithPadding(sb, 6, spec, [&] {
                    sb.Append("(null)", 6);
                });
                return;
            }
            usize len = 0;
            while (value[len]) len++;
            Detail::WriteWithPadding(sb, len, spec, [&] {
                sb.Append(value, len);
            });
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
            Detail::WriteWithPadding(sb, len, spec, [&] {
                sb.Append(s, len);
            });
        }
    };

    template <Integral T>
    struct Formatter<T> {
        template <typename Sink>
        void Format(Sink& sb, const T& value, const FormatSpec& spec = {}) {
            char buf[128]; 

            usize len;
            if constexpr (Signed<T>) len = Detail::SignedToChars(value, buf, spec.base, spec.uppercase);
            else len = Detail::UnsignedToChars(value, buf, spec.base, spec.uppercase);
            
            // Handle signs for base 10
            char sign_char = '\0';
            if (spec.base == 10 || spec.base == 0) {
                if (buf[0] == '-') {
                    // Sign is already in buf
                } else {
                    if (spec.sign == Sign::Plus) { sign_char = '+'; }
                    else if (spec.sign == Sign::Space) { sign_char = ' '; }
                }
            }

            usize prefix_len = 0;
            char prefix[2];
            if (spec.prefix) {
                if (spec.base == 16) { prefix[0] = '0'; prefix[1] = spec.uppercase ? 'X' : 'x'; prefix_len = 2; }
                else if (spec.base == 2) { prefix[0] = '0'; prefix[1] = 'b'; prefix_len = 2; }
                else if (spec.base == 8) { prefix[0] = '0'; prefix_len = 1; }
            }

            usize total_len = len + prefix_len + (sign_char ? 1 : 0);
            
            auto write_content = [&] {
                if (sign_char) sb.Append(sign_char);
                if (prefix_len > 0) sb.Append(prefix, prefix_len);
                sb.Append(buf, len);
            };

            if (spec.zero_pad) {
                if (sign_char) sb.Append(sign_char);
                if (prefix_len > 0) sb.Append(prefix, prefix_len);
                
                if (spec.width > total_len) {
                    usize padding = spec.width - total_len;
                    for (usize i = 0; i < padding; ++i) sb.Append('0');
                }
                sb.Append(buf, len);
            } else {
                FormatSpec adjusted_spec = spec;
                if (adjusted_spec.align == Align::None) adjusted_spec.align = Align::Right;
                Detail::WriteWithPadding(sb, total_len, adjusted_spec, write_content);
            }
        }
    };

    template <Pointer T>
    struct Formatter<T> {
        template <typename Sink>
        void Format(Sink& sb, const T& value, const FormatSpec& spec = {}) {
            if (!value) {
                Detail::WriteWithPadding(sb, 4, spec, [&] {
                    sb.Append("null", 4);
                });
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
             bool negative = false;
             if (val < 0) { negative = true; val = -val; }

             char sign_char = '\0';
             if (negative) sign_char = '-';
             else if (spec.sign == Sign::Plus) sign_char = '+';
             else if (spec.sign == Sign::Space) sign_char = ' ';

             u64 integral = static_cast<u64>(val);
             T fraction = val - static_cast<T>(integral);
             
             // Rough estimate of length
             // This is hard without fully formatting to a buffer first.
             // For simplicity, we'll just format directly and ignore padding for now,
             // or format to a temporary buffer.
             
             if (sign_char) sb.Append(sign_char);
             Formatter<u64>().Format(sb, integral);
             sb.Append('.');

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
