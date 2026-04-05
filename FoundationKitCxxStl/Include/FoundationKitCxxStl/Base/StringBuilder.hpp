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

        StringBuilder& Append(const char* data, usize size) {
            m_buffer.Append(StringView(data, size));
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
            const char* data = fmt.Data();
            const usize size = fmt.Size();
            usize arg_index = 0;

            for (usize i = 0; i < size; ++i) {
                if (data[i] == '{') {
                    if (i + 1 < size && data[i + 1] == '{') {
                        Append('{');
                        i++;
                        continue;
                    }

                    // Look for matching '}'
                    usize j = i + 1;
                    while (j < size && data[j] != '}') j++;

                    FK_BUG_ON(j >= size,
                        "StringBuilder::Format: unclosed '{{' in format string at position ({})", i);

                    if (j < size) {
                        // We found { ... }
                        // Parse spec if any
                        FormatSpec spec;
                        const char* spec_begin = data + i + 1;
                        if (*spec_begin == ':') {
                            spec_begin++;
                            Detail::ParseSpec(spec_begin, data + j, spec);
                        }

                        usize current_idx = 0;
                        auto dispatcher = [&]<typename T0>(T0&& arg) {
                            if (current_idx == arg_index) {
                                Formatter<Unqualified<T0>>().Format(*this, FoundationKitCxxStl::Forward<T0>(arg), spec);
                            }
                            current_idx++;
                        };

                        (dispatcher(args), ...);

                        arg_index++;
                        i = j;
                        continue;
                    }
                } else if (data[i] == '}' && i + 1 < size && data[i + 1] == '}') {
                    Append('}');
                    i++;
                    continue;
                }
                
                Append(data[i]);
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

} // namespace FoundationKitCxxStl
