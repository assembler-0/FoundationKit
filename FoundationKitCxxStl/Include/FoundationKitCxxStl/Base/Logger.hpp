#pragma once

#include <FoundationKitCxxStl/Base/Format.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitCxxStl {

    /// @brief Log levels for the FoundationKit logging system.
    enum class LogLevel {
        Fatal,
        Error,
        Warning,
        Info
    };

    namespace Detail {
        /// @brief A minimal, internal string view for logger to avoid circular dependency with StringView.hpp
        struct LoggerStringView {
            const char* data;
            usize size;

            constexpr LoggerStringView() : data(nullptr), size(0) {}
            constexpr LoggerStringView(const char* s) : data(s), size(0) {
                if (s) {
                    while (s[size]) size++;
                }
            }
            constexpr LoggerStringView(const char* s, usize n) : data(s), size(n) {}
        };
    }

    /// @brief A stack-based string builder for formatting without heap allocation.
    /// @tparam Capacity Maximum size of the string including the null terminator.
    template <usize Capacity>
    class StaticStringBuilder {
    public:
        StaticStringBuilder() {
            m_buffer[0] = '\0';
        }

        StaticStringBuilder& Append(const char* data, usize size) {
            const usize to_copy = (m_size + size > Capacity - 1) ? (Capacity - 1 - m_size) : size;
            if (to_copy > 0) {
                for (usize i = 0; i < to_copy; ++i) {
                    m_buffer[m_size + i] = data[i];
                }
                m_size += to_copy;
                m_buffer[m_size] = '\0';
            }
            return *this;
        }

        StaticStringBuilder& Append(const Detail::LoggerStringView view) {
            return Append(view.data, view.size);
        }

        StaticStringBuilder& Append(const char c) {
            if (m_size < Capacity - 1) {
                m_buffer[m_size++] = c;
                m_buffer[m_size] = '\0';
            }
            return *this;
        }

        template <typename T>
        StaticStringBuilder& Append(const T& value, const FormatSpec& spec = {}) {
            Formatter<Unqualified<T>>().Format(*this, value, spec);
            return *this;
        }

        template <typename... Args>
        StaticStringBuilder& Format(const Detail::LoggerStringView fmt, Args&&... args) {
            const char* data = fmt.data;
            const usize size = fmt.size;
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
                                this->Append(FoundationKitCxxStl::Forward<T0>(arg), spec);
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

        [[nodiscard]] const char* CStr() const noexcept { return m_buffer; }
        [[nodiscard]] usize Size() const noexcept { return m_size; }

    private:
        char m_buffer[Capacity]{};
        usize m_size = 0;
    };

    // Specialization for internal string view
    template <>
    struct Formatter<Detail::LoggerStringView> {
        template <typename Sink>
        void Format(Sink& sb, const Detail::LoggerStringView& value) {
            sb.Append(value.data, value.size);
        }
    };

    namespace Detail {
        inline const char* GetLogLevelPrefix(LogLevel level) {
            switch (level) {
                case LogLevel::Fatal:   return "#!(FF)[FoundationKit]: ";
                case LogLevel::Error:   return "#!(EE)[FoundationKit]: ";
                case LogLevel::Warning: return "#!(WW)[FoundationKit]: ";
                case LogLevel::Info:    return "#!(II)[FoundationKit]: ";
            }
            return "#!(##)[FoundationKit]: ";
        }
    }

    /// @brief Logs a message without formatting (early use).
    inline void Log(LogLevel level, Detail::LoggerStringView msg) {
        StaticStringBuilder<1024> sb;
        sb.Append(Detail::GetLogLevelPrefix(level));
        sb.Append(msg);
        sb.Append('\n');
        if (level == LogLevel::Fatal) {
             ::FoundationKitOsl::OslBug(sb.CStr());
        } else {
             ::FoundationKitOsl::OslLog(sb.CStr());
        }
    }

    /// @brief Logs a formatted message.
    template <typename... Args>
    void LogFmt(LogLevel level, Detail::LoggerStringView fmt, Args&&... args) {
        StaticStringBuilder<1024> sb;
        sb.Append(Detail::GetLogLevelPrefix(level));
        sb.Format(fmt, FoundationKitCxxStl::Forward<Args>(args)...);
        sb.Append('\n');
        if (level == LogLevel::Fatal) {
            ::FoundationKitOsl::OslBug(sb.CStr());
        } else {
            ::FoundationKitOsl::OslLog(sb.CStr());
        }
    }

} // namespace FoundationKitCxxStl

/// @brief Log informational message.
#define FK_LOG_INFO(fmt, ...) ::FoundationKitCxxStl::LogFmt(::FoundationKitCxxStl::LogLevel::Info, FoundationKitCxxStl::Detail::LoggerStringView(fmt) __VA_OPT__(,) __VA_ARGS__)

/// @brief Log warning message.
#define FK_LOG_WARN(fmt, ...) ::FoundationKitCxxStl::LogFmt(::FoundationKitCxxStl::LogLevel::Warning, FoundationKitCxxStl::Detail::LoggerStringView(fmt) __VA_OPT__(,) __VA_ARGS__)

/// @brief Log error message.
#define FK_LOG_ERR(fmt, ...)  ::FoundationKitCxxStl::LogFmt(::FoundationKitCxxStl::LogLevel::Error, FoundationKitCxxStl::Detail::LoggerStringView(fmt) __VA_OPT__(,) __VA_ARGS__)

#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

/// @brief Reports a fatal bug and halts execution.
#define FK_BUG(fmt, ...)                                                                                                 \
    do {                                                                                                                 \
        ::FoundationKitCxxStl::LogFmt(::FoundationKitCxxStl::LogLevel::Fatal,                                            \
                                      FoundationKitCxxStl::Detail::LoggerStringView(                                     \
                                          fmt " at " __FILE_NAME__ ":" FOUNDATIONKIT_STR(__LINE__)) __VA_OPT__(, )       \
                                          __VA_ARGS__);                                                                  \
        ::FoundationKitCxxStl::Base::CompilerBuiltins::Unreachable();                                                                                         \
    } while (0)
