#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/Optional.hpp>

namespace FoundationKit {

    /// @brief A fixed-size array container.
    /// @tparam T The type of elements.
    /// @tparam N The number of elements.
    template <typename T, usize N>
    struct FixedArray {
        using ValueType      = T;
        using SizeType       = usize;
        using DifferenceType = isize;
        using Reference      = T&;
        using ConstReference = const T&;
        using Pointer        = T*;
        using ConstPointer   = const T*;
        using Iterator       = T*;
        using ConstIterator  = const T*;

        T DataBuffer[N];

        /// @brief Access element with bounds checking.
        [[nodiscard]] constexpr Optional<Reference> At(SizeType index) noexcept {
            if (index >= N) return NullOpt;
            return DataBuffer[index];
        }

        /// @brief Access element with bounds checking (const).
        [[nodiscard]] constexpr Optional<ConstReference> At(SizeType index) const noexcept {
            if (index >= N) return NullOpt;
            return DataBuffer[index];
        }

        /// @brief Unsafe access operator.
        [[nodiscard]] constexpr Reference operator[](SizeType index) noexcept {
            return DataBuffer[index];
        }

        /// @brief Unsafe access operator (const).
        [[nodiscard]] constexpr ConstReference operator[](SizeType index) const noexcept {
            return DataBuffer[index];
        }

        [[nodiscard]] constexpr Reference Front() noexcept { 
            static_assert(N > 0, "FoundationKit: FixedArray must not be empty to call Front()");
            return DataBuffer[0]; 
        }
        
        [[nodiscard]] constexpr ConstReference Front() const noexcept { 
            static_assert(N > 0, "FoundationKit: FixedArray must not be empty to call Front()");
            return DataBuffer[0]; 
        }

        [[nodiscard]] constexpr Reference Back() noexcept { 
            static_assert(N > 0, "FoundationKit: FixedArray must not be empty to call Back()");
            return DataBuffer[N - 1]; 
        }
        
        [[nodiscard]] constexpr ConstReference Back() const noexcept { 
            static_assert(N > 0, "FoundationKit: FixedArray must not be empty to call Back()");
            return DataBuffer[N - 1]; 
        }

        [[nodiscard]] constexpr Pointer Data() noexcept { return DataBuffer; }
        [[nodiscard]] constexpr ConstPointer Data() const noexcept { return DataBuffer; }

        [[nodiscard]] constexpr Iterator Begin() noexcept { return DataBuffer; }
        [[nodiscard]] constexpr ConstIterator Begin() const noexcept { return DataBuffer; }
        
        [[nodiscard]] constexpr Iterator End() noexcept { return DataBuffer + N; }
        [[nodiscard]] constexpr ConstIterator End() const noexcept { return DataBuffer + N; }

        [[nodiscard]] constexpr Iterator begin() noexcept { return Begin(); }
        [[nodiscard]] constexpr ConstIterator begin() const noexcept { return Begin(); }
        [[nodiscard]] constexpr Iterator end() noexcept { return End(); }
        [[nodiscard]] constexpr ConstIterator end() const noexcept { return End(); }

        [[nodiscard]] static constexpr SizeType Size() noexcept { return N; }
        [[nodiscard]] static constexpr bool Empty() noexcept { return N == 0; }
        
        constexpr void Fill(const T& value) noexcept {
            for (usize i = 0; i < N; ++i)
                DataBuffer[i] = value;
        }

        constexpr void Swap(FixedArray& other) noexcept {
            for (usize i = 0; i < N; ++i) {
                FoundationKit::Swap(DataBuffer[i], other.DataBuffer[i]);
            }
        }
    };

    /// @brief Specialization for empty arrays.
    template <typename T>
    struct FixedArray<T, 0> {
        using ValueType      = T;
        using SizeType       = usize;
        using DifferenceType = isize;
        using Reference      = T&;
        using ConstReference = const T&;
        using Pointer        = T*;
        using ConstPointer   = const T*;
        using Iterator       = T*;
        using ConstIterator  = const T*;

        [[nodiscard]] constexpr Optional<Reference> At(SizeType) noexcept { return NullOpt; }
        [[nodiscard]] constexpr Optional<ConstReference> At(SizeType) const noexcept { return NullOpt; }

        [[nodiscard]] constexpr Pointer Data() noexcept { return nullptr; }
        [[nodiscard]] constexpr ConstPointer Data() const noexcept { return nullptr; }

        [[nodiscard]] constexpr Iterator Begin() noexcept { return nullptr; }
        [[nodiscard]] constexpr ConstIterator Begin() const noexcept { return nullptr; }
        
        [[nodiscard]] constexpr Iterator End() noexcept { return nullptr; }
        [[nodiscard]] constexpr ConstIterator End() const noexcept { return nullptr; }

        [[nodiscard]] constexpr Iterator begin() noexcept { return nullptr; }
        [[nodiscard]] constexpr ConstIterator begin() const noexcept { return nullptr; }
        [[nodiscard]] constexpr Iterator end() noexcept { return nullptr; }
        [[nodiscard]] constexpr ConstIterator end() const noexcept { return nullptr; }

        [[nodiscard]] static constexpr SizeType Size() noexcept { return 0; }
        [[nodiscard]] static constexpr bool Empty() noexcept { return true; }

        static constexpr void Fill(const T&) noexcept {}
        static constexpr void Swap(FixedArray&) noexcept {}
    };

    template <typename T, typename... U>
    FixedArray(T, U...) -> FixedArray<T, 1 + sizeof...(U)>;

} // namespace FoundationKit
