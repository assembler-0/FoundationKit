#pragma once

#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitCxxStl {

    /// @brief Represents an unexpected value.
    template <typename E>
    class Unexpected {
    public:
        constexpr explicit Unexpected(const E& error) : _error(error) {}
        constexpr explicit Unexpected(E&& error) : _error(FoundationKitCxxStl::Move(error)) {}

        template <typename... Args>
        constexpr explicit Unexpected(InPlaceT, Args&&... args)
            : _error(FoundationKitCxxStl::Forward<Args>(args)...) {}

        constexpr const E& Error() const& noexcept { return _error; }
        constexpr E& Error() & noexcept { return _error; }
        constexpr E&& Error() && noexcept { return FoundationKitCxxStl::Move(_error); }
        constexpr const E&& Error() const&& noexcept { return FoundationKitCxxStl::Move(_error); }

    private:
        E _error;
    };

    template <typename E>
    Unexpected(E) -> Unexpected<E>;

    /// @brief Represents either a value or an error.
    template <typename T, typename E>
    class Expected {
    public:
        using ValueType = T;
        using ErrorType = E;

        /// @brief Default constructor (empty).
        constexpr Expected() noexcept requires SameAs<T, void> : _value{}, _has_value(true) {}

        /// @brief Construct with value (Copy).
        template <typename U = T>
        requires (!SameAs<U, void> && !SameAs<U, Unexpected<E>> && !SameAs<U, Expected<T, E>> && !SameAs<U, E>)
        constexpr Expected(const T& value) noexcept : _value(value), _has_value(true) {}

        /// @brief Construct with value (Move).
        template <typename U = T>
        requires (!SameAs<U, void> && !SameAs<U, Unexpected<E>> && !SameAs<U, Expected<T, E>> && !SameAs<U, E>)
        constexpr Expected(T&& value) noexcept : _value(FoundationKitCxxStl::Move(value)), _has_value(true) {}

        /// @brief Construct from Unexpected (Copy).
        constexpr Expected(const Unexpected<E>& u) noexcept : _error(u.Error()), _has_value(false) {}

        /// @brief Construct from Unexpected (Move).
        constexpr Expected(Unexpected<E>&& u) noexcept : _error(FoundationKitCxxStl::Move(u.Error())), _has_value(false) {}

        template <typename... Args>
        constexpr explicit Expected(InPlaceT, Args&&... args)
            : _value(FoundationKitCxxStl::Forward<Args>(args)...), _has_value(true) {}

        /// @brief Construct with error (Copy).
        template <typename U = E>
        requires (!SameAs<T, U>)
        [[deprecated("Use Unexpected<E> instead")]]
        constexpr Expected(const E& error) noexcept : _error(error), _has_value(false) {}

        /// @brief Construct with error (Move).
        template <typename U = E>
        requires (!SameAs<T, U>)
        [[deprecated("Use Unexpected<E> instead")]]
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
            if (!_has_value) [[unlikely]] {
                FK_BUG("Expected: access to value while holding {}", *this);
            }
            return _value;
        }

        [[nodiscard]] constexpr const T& Value() const& noexcept {
            if (!_has_value) [[unlikely]] {
                FK_BUG("Expected: access to value while holding {}", *this);
            }
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
            if (_has_value) [[unlikely]] {
                FK_BUG("Expected: access to error while holding {}", *this);
            }
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

        /// @brief Construct from Unexpected (Copy).
        constexpr Expected(const Unexpected<E>& u) noexcept : _error(u.Error()), _has_value(false) {}

        /// @brief Construct from Unexpected (Move).
        constexpr Expected(Unexpected<E>&& u) noexcept : _error(FoundationKitCxxStl::Move(u.Error())), _has_value(false) {}

        /// @brief Construct with error (Copy).
        [[deprecated("Use Unexpected<E> instead")]]
        explicit constexpr Expected(const E& error) noexcept : _error(error), _has_value(false) {}

        /// @brief Construct with error (Move).
        [[deprecated("Use Unexpected<E> instead")]]
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

    /// @brief Formatter for Expected<T, E>.
    template <typename T, typename E>
    struct Formatter<Expected<T, E>> {
        template <typename Sink>
        void Format(Sink& sb, const Expected<T, E>& value, const FormatSpec& spec = {}) {
            if (value.HasValue()) {
                sb.Append("Value(", 6);
                if constexpr (!SameAs<T, void>) {
                    Formatter<Unqualified<T>>().Format(sb, *value, spec);
                } else {
                    sb.Append("void", 4);
                }
                sb.Append(')');
            } else {
                sb.Append("Error(", 6);
                Formatter<Unqualified<E>>().Format(sb, value.Error(), spec);
                sb.Append(')');
            }
        }
    };

} // namespace FoundationKitCxxStl
