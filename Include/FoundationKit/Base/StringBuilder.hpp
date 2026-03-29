#pragma once

#include <FoundationKit/Base/String.hpp>
#include <FoundationKit/Base/StringView.hpp>
#include <FoundationKit/Base/NumericLimits.hpp>

namespace FoundationKit {

    namespace Detail {
        template <Unsigned T>
        constexpr usize UnsignedToChars(T value, char* buffer) {
            if (value == 0) {
                buffer[0] = '0';
                return 1;
            }

            usize i = 0;
            while (value > 0) {
                buffer[i++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }

            for (usize j = 0; j < i / 2; ++j) {
                Swap(buffer[j], buffer[i - j - 1]);
            }

            return i;
        }

        template <Signed T>
        constexpr usize SignedToChars(T value, char* buffer) {
            if (value == 0) {
                buffer[0] = '0';
                return 1;
            }

            usize i = 0;
            bool negative = false;
            if (value < 0) {
                negative = true;
                // Handle Min() case by using unsigned math
                u64 uval = static_cast<u64>(-(value + 1)) + 1;
                while (uval > 0) {
                    buffer[i++] = static_cast<char>('0' + (uval % 10));
                    uval /= 10;
                }
            } else {
                u64 uval = static_cast<u64>(value);
                while (uval > 0) {
                    buffer[i++] = static_cast<char>('0' + (uval % 10));
                    uval /= 10;
                }
            }

            if (negative) buffer[i++] = '-';

            for (usize j = 0; j < i / 2; ++j) {
                Swap(buffer[j], buffer[i - j - 1]);
            }

            return i;
        }
    }

    /// @brief Efficiently build strings with {} formatting.
    class StringBuilder {
    public:
        explicit StringBuilder(Memory::AnyAllocator allocator = Memory::AnyAllocator())
            : m_buffer(Move(allocator)) {}

        StringBuilder& Append(StringView view) {
            m_buffer.Append(view);
            return *this;
        }

        StringBuilder& Append(char c) {
            char buf[2] = {c, '\0'};
            m_buffer.Append(StringView(buf, 1));
            return *this;
        }

        template <Integral T>
        StringBuilder& Append(T value) {
            char buf[32];
            usize len;
            if constexpr (Signed<T>) len = Detail::SignedToChars(value, buf);
            else len = Detail::UnsignedToChars(value, buf);
            m_buffer.Append(StringView(buf, len));
            return *this;
        }

        template <typename... Args>
        StringBuilder& Format(StringView fmt, Args&&... args) {
            usize arg_index = 0;
            const char* data = fmt.Data();
            const usize size = fmt.Size();

            for (usize i = 0; i < size; ++i) {
                if (data[i] == '{' && i + 1 < size && data[i + 1] == '}') {
                    usize current_idx = 0;
                    auto dispatcher = [&]<typename T0>(T0&& arg) {
                        if (current_idx == arg_index) {
                            this->Append(FoundationKit::Forward<T0>(arg));
                        }
                        current_idx++;
                    };

                    (dispatcher(args), ...);

                    arg_index++;
                    i++; // skip '}'
                } else {
                    Append(data[i]);
                }
            }
            return *this;
        }

        [[nodiscard]] String<> Build() && {
            return Move(m_buffer);
        }

        [[nodiscard]] StringView View() const noexcept {
            return static_cast<StringView>(m_buffer);
        }

        [[nodiscard]] const char* CStr() const noexcept {
            return m_buffer.CStr();
        }

    private:
        String<> m_buffer;
    };

} // namespace FoundationKit
