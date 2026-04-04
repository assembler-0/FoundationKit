#pragma once

#include <FoundationKitCxxStl/Base/String.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/NumericLimits.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>

namespace FoundationKitCxxStl {

    /// @brief Efficiently build strings with {} formatting.
    class StringBuilder {
    public:
        explicit StringBuilder(FoundationKitMemory::AnyAllocator allocator = FoundationKitMemory::AnyAllocator())
            : m_buffer(Move(allocator)) {}

        StringBuilder& Append(const StringView view) {
            m_buffer.Append(view);
            return *this;
        }

        StringBuilder& Append(const char c) {
            const char buf[2] = {c, '\0'};
            m_buffer.Append(StringView(buf, 1));
            return *this;
        }

        template <typename T>
        StringBuilder& Append(const T& value) {
            Formatter<Unqualified<T>>().Format(*this, value);
            return *this;
        }

        template <typename... Args>
        StringBuilder& Format(const StringView fmt, Args&&... args) {
            usize arg_index = 0;
            const char* data = fmt.Data();
            const usize size = fmt.Size();

            for (usize i = 0; i < size; ++i) {
                if (data[i] == '{' && i + 1 < size && data[i + 1] == '}') {
                    usize current_idx = 0;
                    auto dispatcher = [&]<typename T0>(T0&& arg) {
                        if (current_idx == arg_index) {
                            this->Append(FoundationKitCxxStl::Forward<T0>(arg));
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

    // Specializations for basic types

    template <>
    struct Formatter<char> {
        void Format(StringBuilder& sb, char value) {
            sb.Append(value);
        }
    };

    template <>
    struct Formatter<StringView> {
        void Format(StringBuilder& sb, StringView value) {
            sb.Append(value);
        }
    };

    template <usize N>
    struct Formatter<char[N]> {
        void Format(StringBuilder& sb, const char* value) {
            sb.Append(StringView(value));
        }
    };

    template <>
    struct Formatter<const char*> {
        void Format(StringBuilder& sb, const char* value) {
            sb.Append(StringView(value));
        }
    };

    template <Integral T>
    struct Formatter<T> {
        void Format(StringBuilder& sb, T value) {
            char buf[64];
            usize len;
            if constexpr (Signed<T>) len = Detail::SignedToChars(value, buf);
            else len = Detail::UnsignedToChars(value, buf);
            sb.Append(StringView(buf, len));
        }
    };

} // namespace FoundationKitCxxStl
