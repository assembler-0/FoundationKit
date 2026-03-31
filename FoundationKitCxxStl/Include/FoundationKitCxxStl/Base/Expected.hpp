#pragma once

#include <FoundationKitCxxStl/Base/Utility.hpp>

namespace FoundationKitCxxStl {

    /// @brief Represents either a value or an error.
    template <typename T, typename E>
    class Expected {
    public:
        using ValueType = T;
        using ErrorType = E;

        /// @brief Default constructor (empty).
        constexpr Expected() noexcept requires SameAs<T, void> : _value{}, _has_value(true) {}

        /// @brief Construct with value (Copy).
        constexpr Expected(const T& value) noexcept : _value(value), _has_value(true) {}

        /// @brief Construct with value (Move).
        constexpr Expected(T&& value) noexcept : _value(FoundationKitCxxStl::Move(value)), _has_value(true) {}

        /// @brief Construct with error (Copy).
        constexpr Expected(const E& error) noexcept : _error(error), _has_value(false) {}

        /// @brief Construct with error (Move).
        constexpr Expected(E&& error) noexcept : _error(FoundationKitCxxStl::Move(error)), _has_value(false) {}

        ~Expected() noexcept {
            if (_has_value) {
                if constexpr (!SameAs<T, void>) _value.~T();
            } else {
                _error.~E();
            }
        }

        [[nodiscard]] constexpr bool HasValue() const noexcept { return _has_value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return _has_value; }

        [[nodiscard]] constexpr T& Value() & noexcept {
            FK_BUG_ON(!_has_value, "Expected: access to value while error is present");
            return _value;
        }

        [[nodiscard]] constexpr const T& Value() const& noexcept {
            FK_BUG_ON(!_has_value, "Expected: access to value while error is present");
            return _value;
        }

        [[nodiscard]] constexpr T* operator->() noexcept { return &Value(); }
        [[nodiscard]] constexpr const T* operator->() const noexcept { return &Value(); }

        [[nodiscard]] constexpr T& operator*() & noexcept { return Value(); }
        [[nodiscard]] constexpr const T& operator*() const& noexcept { return Value(); }

        [[nodiscard]] constexpr E& Error() & noexcept {
            FK_BUG_ON(_has_value, "Expected: access to error while value is present");
            return _error;
        }

        [[nodiscard]] constexpr const E& Error() const& noexcept {
            FK_BUG_ON(_has_value, "Expected: access to error while value is present");
            return _error;
        }

    private:
        union {
            T _value;
            E _error;
        };
        bool _has_value;
    };

    /// @brief Specialization for void value type.
    template <typename E>
    class Expected<void, E> {
    public:
        using ValueType = void;
        using ErrorType = E;

        constexpr Expected() noexcept : _has_value(true) {}

        /// @brief Construct with error (Copy).
        explicit constexpr Expected(const E& error) noexcept : _error(error), _has_value(false) {}

        /// @brief Construct with error (Move).
        explicit constexpr Expected(E&& error) noexcept : _error(FoundationKitCxxStl::Move(error)), _has_value(false) {}

        ~Expected() noexcept {
            if (!_has_value) _error.~E();
        }

        [[nodiscard]] constexpr bool HasValue() const noexcept { return _has_value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return _has_value; }

        constexpr void Value() const noexcept {
            FK_BUG_ON(!_has_value, "Expected<void>: access to value while error is present");
        }

        [[nodiscard]] constexpr E& Error() & noexcept {
            FK_BUG_ON(_has_value, "Expected<void>: access to error while value is present");
            return _error;
        }

        [[nodiscard]] constexpr const E& Error() const& noexcept {
            FK_BUG_ON(_has_value, "Expected<void>: access to error while value is present");
            return _error;
        }

    private:
        union {
            char _dummy{};
            E _error;
        };
        bool _has_value;
    };

} // namespace FoundationKitCxxStl
