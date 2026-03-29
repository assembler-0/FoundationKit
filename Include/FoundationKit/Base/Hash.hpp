#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/StringView.hpp>
#include <FoundationKit/Meta/Concepts.hpp>

namespace FoundationKit {

    /// @brief FNV-1a hash implementation.
    struct Hash {
        static constexpr u64 OffsetBasis = 0xcbf29ce484222325ULL;
        static constexpr u64 Prime = 0x100000001b3ULL;

        template <typename T>
        requires Integral<T> || Enum<T>
        [[nodiscard]] constexpr u64 operator()(T value) const noexcept {
            u64 h = OffsetBasis;
            const auto data = reinterpret_cast<const byte*>(&value);
            for (usize i = 0; i < sizeof(T); ++i) {
                h ^= data[i];
                h *= Prime;
            }
            return h;
        }

        [[nodiscard]] constexpr u64 operator()(const void* ptr) const noexcept {
            return operator()(reinterpret_cast<uptr>(ptr));
        }

        [[nodiscard]] constexpr u64 operator()(StringView view) const noexcept {
            u64 h = OffsetBasis;
            for (usize i = 0; i < view.Size(); ++i) {
                h ^= static_cast<byte>(view.Data()[i]);
                h *= Prime;
            }
            return h;
        }

        [[nodiscard]] constexpr u64 operator()(const char* str) const noexcept {
            return operator()(StringView(str));
        }
    };

} // namespace FoundationKit
