#pragma once

#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Osl/Osl.hpp>

namespace FoundationKit {

    /// @brief Tag type for empty optional.
    struct NullOptT {
        explicit constexpr NullOptT(int) {}
    };

    /// @brief Global instance of NullOptT.
    inline constexpr NullOptT NullOpt{0};

    /// @brief A wrapper that may or may not contain a value.
    template <typename T>
    class Optional {
    public:
        using ValueType = T;

        /// @brief Default constructor (empty).
        constexpr Optional() noexcept : _dummy(), _has_value(false) {}

        /// @brief Construct as empty.
        constexpr Optional(NullOptT) noexcept : _dummy(), _has_value(false) {}

        /// @brief Construct with value (Copy).
        constexpr Optional(const T& value) noexcept : _value(value), _has_value(true) {}

        /// @brief Construct with value (Move).
        constexpr Optional(T&& value) noexcept : _value(FoundationKit::Move(value)), _has_value(true) {}

        /// @brief Copy constructor.
        constexpr Optional(const Optional& other) noexcept : _has_value(other._has_value) {
            if (other._has_value) {
                FoundationKit::ConstructAt<T>(&_value, other._value);
            } else {
                _dummy = 0;
            }
        }

        /// @brief Move constructor.
        constexpr Optional(Optional&& other) noexcept : _has_value(other._has_value) {
            if (other._has_value) {
                FoundationKit::ConstructAt<T>(&_value, FoundationKit::Move(other._value));
            } else {
                _dummy = 0;
            }
        }

        /// @brief Destructor.
        ~Optional() noexcept {
            Reset();
        }

        /// @brief Assign from NullOpt.
        Optional& operator=(NullOptT) noexcept {
            Reset();
            return *this;
        }

        /// @brief Assign from value (Copy).
        Optional& operator=(const T& value) noexcept {
            if (_has_value) {
                _value = value;
            } else {
                FoundationKit::ConstructAt<T>(&_value, value);
                _has_value = true;
            }
            return *this;
        }

        /// @brief Assign from value (Move).
        Optional& operator=(T&& value) noexcept {
            if (_has_value) {
                _value = FoundationKit::Move(value);
            } else {
                FoundationKit::ConstructAt<T>(&_value, FoundationKit::Move(value));
                _has_value = true;
            }
            return *this;
        }

        /// @brief Copy assignment.
        Optional& operator=(const Optional& other) noexcept {
            if (this == &other) return *this;
            if (other._has_value) {
                *this = other._value;
            } else {
                Reset();
            }
            return *this;
        }

        /// @brief Move assignment.
        Optional& operator=(Optional&& other) noexcept {
            if (this == &other) return *this;
            if (other._has_value) {
                *this = FoundationKit::Move(other._value);
            } else {
                Reset();
            }
            return *this;
        }

        /// @brief Checks if a value is present.
        [[nodiscard]] constexpr bool HasValue() const noexcept { return _has_value; }

        /// @brief Boolean conversion.
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return _has_value; }

        /// @brief Access value.
        [[nodiscard]] constexpr T& Value() & noexcept {
            if (!_has_value) FOUNDATIONKIT_PANIC("!_has_value");
            return _value;
        }

        /// @brief Access value (const).
        [[nodiscard]] constexpr const T& Value() const& noexcept {
            if (!_has_value) FOUNDATIONKIT_PANIC("!_has_value");
            return _value;
        }

        /// @brief Access value via pointer.
        [[nodiscard]] constexpr T* operator->() noexcept { return &Value(); }
        [[nodiscard]] constexpr const T* operator->() const noexcept { return &Value(); }

        /// @brief Access value via reference.
        [[nodiscard]] constexpr T& operator*() & noexcept { return Value(); }
        [[nodiscard]] constexpr const T& operator*() const& noexcept { return Value(); }

        /// @brief Returns the value or a default.
        template <typename U>
        [[nodiscard]] constexpr T ValueOr(U&& default_value) const& noexcept {
            return _has_value ? _value : static_cast<T>(FoundationKit::Forward<U>(default_value));
        }

        /// @brief Destroys the contained value.
        void Reset() noexcept {
            if (_has_value) {
                _value.~T();
                _has_value = false;
                _dummy = 0;
            }
        }

    private:
        union {
            T _value;
            char _dummy;
        };
        bool _has_value;
    };

    /// @brief Specialization for references.
    template <typename T>
    class Optional<T&> {
    public:
        using ValueType = T&;

        constexpr Optional() noexcept : _ptr(nullptr) {}
        constexpr Optional(NullOptT) noexcept : _ptr(nullptr) {}
        constexpr Optional(T& value) noexcept : _ptr(&value) {}

        constexpr Optional(const Optional& other) noexcept = default;
        constexpr Optional(Optional&& other) noexcept = default;

        ~Optional() noexcept = default;

        Optional& operator=(NullOptT) noexcept {
            _ptr = nullptr;
            return *this;
        }

        Optional& operator=(T& value) noexcept {
            _ptr = &value;
            return *this;
        }

        Optional& operator=(const Optional& other) noexcept = default;
        Optional& operator=(Optional&& other) noexcept = default;

        [[nodiscard]] constexpr bool HasValue() const noexcept { return _ptr != nullptr; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return _ptr != nullptr; }

        [[nodiscard]] constexpr T& Value() const noexcept {
            if (!_ptr) FOUNDATIONKIT_PANIC("!_ptr");
            return *_ptr;
        }

        [[nodiscard]] constexpr T* operator->() const noexcept { return _ptr; }
        [[nodiscard]] constexpr T& operator*() const noexcept { return *_ptr; }

        template <typename U>
        [[nodiscard]] constexpr T& ValueOr(U&& default_value) const noexcept {
            return _ptr ? *_ptr : static_cast<T&>(FoundationKit::Forward<U>(default_value));
        }

        void Reset() noexcept { _ptr = nullptr; }

    private:
        T* _ptr;
    };

} // namespace FoundationKit
