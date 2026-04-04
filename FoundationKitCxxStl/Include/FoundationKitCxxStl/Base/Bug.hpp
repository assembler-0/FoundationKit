#pragma once

#include <FoundationKitCxxStl/Base/Compiler.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>

/// @brief Asserts that a condition is true, otherwise triggers a fatal OSL bug.
#define FK_BUG_ON(condition, msg, ...)                                                          \
    do {                                                                                        \
        if (!!(condition)) [[unlikely]] {                                                       \
            FK_BUG(msg " (" #condition ")" __VA_OPT__(,) __VA_ARGS__);                          \
        }                                                                                       \
    } while (0)

/// @brief Asserts that a condition is true, otherwise warns host OS.
#define FK_WARN_ON(condition, msg, ...)                                                         \
    do {                                                                                        \
        if (!!(condition)) [[unlikely]] {                                                       \
            FK_LOG_WARN(msg " (" #condition ")" __VA_OPT__(,) __VA_ARGS__);                     \
        }                                                                                       \
    } while (0)
