#pragma once

#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Vector.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>

namespace FoundationKitCxxStl {

    /// @brief A simple command-line parser for OSDev/Freestanding environments.
    class CommandLine {
    public:
        /// @brief Initialize with a raw command line string.
        explicit CommandLine(StringView raw) : m_raw(raw) {
            Parse();
        }

        /// @brief Check if a flag exists (e.g., "debug" or "--force").
        [[nodiscard]] bool HasFlag(StringView name) const noexcept {
            for (const auto& arg : m_arguments) {
                if (arg == name) return true;
            }
            return false;
        }

        /// @brief Get the value of an option (e.g., "log_level=3" returns "3").
        [[nodiscard]] Optional<StringView> GetOption(StringView name) const noexcept {
            for (const auto& arg : m_arguments) {
                if (arg.StartsWith(name) && arg.Size() > name.Size() && arg[name.Size()] == '=') {
                    return arg.SubView(name.Size() + 1);
                }
            }
            return NullOpt;
        }

        /// @brief Get an argument by index.
        [[nodiscard]] Optional<StringView> GetArgument(usize index) const noexcept {
            if (index >= m_arguments.Size()) return NullOpt;
            return m_arguments[index];
        }

        [[nodiscard]] usize ArgumentCount() const noexcept {
            return m_arguments.Size();
        }

    private:
        void Parse() {
            StringView current = m_raw.Trim();
            while (!current.Empty()) {
                usize space = current.Find(' ');
                if (space == static_cast<usize>(-1)) {
                    m_arguments.PushBack(current);
                    break;
                }

                m_arguments.PushBack(current.SubView(0, space));
                current = current.SubView(space).Trim();
            }
        }

        StringView m_raw;
        Vector<StringView> m_arguments;
    };

} // namespace FoundationKitCxxStl
