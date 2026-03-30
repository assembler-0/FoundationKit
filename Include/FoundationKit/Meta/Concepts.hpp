#pragma once

#include <FoundationKit/Base/Types.hpp>

namespace FoundationKit {

    template <typename T, typename U>
    concept SameAs = __is_same(T, U);

    /// @brief Remove const, volatile, and reference qualifiers.
    template <typename T>
    struct RemoveCvRef {
        using Type = __remove_cvref(T);
    };

    template <typename T>
    using Unqualified = RemoveCvRef<T>::Type;

    template <typename From, typename To>
    concept ConvertibleTo = __is_convertible_to(From, To);

    template <typename Base, typename Derived>
    concept BaseOf = __is_base_of(Base, Derived);

    template <typename T> concept Integral        = __is_integral(T);
    template <typename T> concept FloatingPoint    = __is_floating_point(T);
    template <typename T> concept Signed           = __is_signed(T);
    template <typename T> concept Unsigned         = __is_unsigned(T);
    template <typename T> concept Enum             = __is_enum(T);
    template <typename T> concept Union            = __is_union(T);
    template <typename T> concept Class            = __is_class(T);
    template <typename T> concept Pointer          = __is_pointer(T);
    template <typename T> concept MemberPointer    = __is_member_pointer(T);
    template <typename T> concept LValueReference  = __is_lvalue_reference(T);
    template <typename T> concept RValueReference  = __is_rvalue_reference(T);
    template <typename T> concept Reference        = LValueReference<T> || RValueReference<T>;
    template <typename T> concept Void             = __is_void(T);
    template <typename T> concept NullPointer      = SameAs<T, decltype(nullptr)>;
    template <typename T> concept IsArray          = __is_array(T);
    template <typename T> concept Abstract         = __is_abstract(T);
    template <typename T> concept Final            = __is_final(T);
    template <typename T> concept Empty            = __is_empty(T);
    template <typename T> concept Polymorphic      = __is_polymorphic(T);

    template <typename T>
    concept Arithmetic = Integral<T> || FloatingPoint<T>;

    template <typename T>
    concept Scalar = Arithmetic<T> || Pointer<T> || MemberPointer<T>
                  || Enum<T>       || NullPointer<T>;

    template <typename T>
    concept Fundamental = Scalar<T> || Void<T>;

    template <typename T>
    concept Compound = !Fundamental<T>;

    template <typename T> concept Const          = __is_const(T);
    template <typename T> concept Volatile       = __is_volatile(T);
    template <typename T> concept ConstVolatile  = Const<T> && Volatile<T>;

    template <typename T>
    concept ObjectType = !Reference<T> && !Void<T> && !__is_function(T);

    template <typename T>
    concept Referenceable = !Void<T>;

    template <typename T>
    concept DefaultConstructible   = __is_constructible(T);

    template <typename T>
    concept CopyConstructible      = __is_constructible(T, const T&);

    template <typename T>
    concept MoveConstructible      = __is_constructible(T, T&&);

    template <typename T>
    concept CopyAssignable         = __is_assignable(T&, const T&);

    template <typename T>
    concept MoveAssignable         = __is_assignable(T&, T&&);

    template <typename T>
    concept Destructible           = __is_trivially_destructible(T);

    template <typename T>
    concept TriviallyDefaultConstructible = __is_trivially_constructible(T);

    template <typename T>
    concept TriviallyCopyConstructible    = __is_trivially_constructible(T, const T&);

    template <typename T>
    concept TriviallyMoveConstructible    = __is_trivially_constructible(T, T&&);

    template <typename T>
    concept TriviallyCopyAssignable       = __is_trivially_assignable(T&, const T&);

    template <typename T>
    concept TriviallyMoveAssignable       = __is_trivially_assignable(T&, T&&);

    template <typename T>
    concept TriviallyDestructible         = __is_trivially_destructible(T);

    template <typename T>
    concept TriviallyCopyable = __is_trivially_copyable(T);

    template <typename T>
    concept Trivial = __is_trivial(T);

    template <typename T>
    concept StandardLayout = __is_standard_layout(T);

    template <typename T>
    concept POD = Trivial<T> && StandardLayout<T>;

    template <typename T, typename... Args>
    concept ConstructibleFrom = __is_constructible(T, Args...);

    template <typename T, typename U>
    concept AssignableFrom = __is_assignable(T&, U);

    template <typename T>
    concept EqualityComparable = requires(const T& a, const T& b) {
        { a == b } -> ConvertibleTo<bool>;
        { a != b } -> ConvertibleTo<bool>;
    };

    template <typename T, typename U>
    concept EqualityComparableWith = requires(const T& a, const U& b) {
        { a == b } -> ConvertibleTo<bool>;
        { a != b } -> ConvertibleTo<bool>;
        { b == a } -> ConvertibleTo<bool>;
        { b != a } -> ConvertibleTo<bool>;
    };

    template <typename T>
    concept TotallyOrdered = EqualityComparable<T> &&
        requires(const T& a, const T& b) {
            { a <  b } -> ConvertibleTo<bool>;
            { a >  b } -> ConvertibleTo<bool>;
            { a <= b } -> ConvertibleTo<bool>;
            { a >= b } -> ConvertibleTo<bool>;
        };

    template <typename T>
    concept ThreeWayComparable = requires(const T& a, const T& b) {
        { a <=> b };
    };


    template <typename F, typename... Args>
    concept Invocable = requires(F&& f, Args&&... args) {
        static_cast<F&&>(f)(static_cast<Args&&>(args)...);
    };

    template <typename F, typename R, typename... Args>
    concept InvocableR = Invocable<F, Args...> &&
        requires(F&& f, Args&&... args) {
            { static_cast<F&&>(f)(static_cast<Args&&>(args)...) }
                -> ConvertibleTo<R>;
        };

    template <typename F, typename... Args>
    concept Predicate = InvocableR<F, bool, Args...>;

    template <typename F, typename T>
    concept Relation = Predicate<F, T, T>;

    template <typename T>
    T&& DeclVal() noexcept;

    template <typename I>
    concept Iterator = requires(I it) {
        { *it };
        { ++it } -> SameAs<I&>;
    };

    template <typename I>
    using IterValue = Unqualified<decltype(*DeclVal<I>())>;

    template <typename I>
    concept InputIterator = Iterator<I> && EqualityComparable<I>;

    template <typename I, typename T>
    concept OutputIterator = Iterator<I> && requires(I it, T&& val) {
        { *it = static_cast<T&&>(val) };
    };

    template <typename I>
    concept ForwardIterator = InputIterator<I> &&
        DefaultConstructible<I> &&
        requires(I it) {
            { it++ } -> SameAs<I>;
            { *it++ };
        };

    template <typename I>
    concept BidirectionalIterator = ForwardIterator<I> &&
        requires(I it) {
            { --it  } -> SameAs<I&>;
            { it--  } -> SameAs<I>;
        };

    template <typename I>
    concept RandomAccessIterator = BidirectionalIterator<I> &&
        TotallyOrdered<I> &&
        requires(I it, I jt, isize n) {
            { it + n  } -> SameAs<I>;
            { it - n  } -> SameAs<I>;
            { it - jt } -> SameAs<isize>;
            { it[n]   };
        };

    template <typename R>
    concept Range = requires(R& r) {
        { r.begin() } -> Iterator;
        { r.end()   };
    };

    template <typename R>
    concept HomogeneousRange = requires(R& r) {
        { r.begin() } -> ForwardIterator;
        { r.end()   } -> SameAs<decltype(r.begin())>;
    };

    template <typename T>
    struct MakeUnsigned;

    template <> struct MakeUnsigned<i8>   { using Type = u8; };
    template <> struct MakeUnsigned<u8>   { using Type = u8; };
    template <> struct MakeUnsigned<i16>  { using Type = u16; };
    template <> struct MakeUnsigned<u16>  { using Type = u16; };
    template <> struct MakeUnsigned<i32>  { using Type = u32; };
    template <> struct MakeUnsigned<u32>  { using Type = u32; };
    template <> struct MakeUnsigned<i64>  { using Type = u64; };
    template <> struct MakeUnsigned<u64>  { using Type = u64; };

    template <typename T>
    using MakeUnsignedT = typename MakeUnsigned<T>::Type;

    template <typename T, usize N>
    concept FitsInBytes = sizeof(T) <= N;

    template <typename T, usize N>
    concept AlignmentAtMost = alignof(T) <= N;

    template <typename T, usize N>
    concept StorableInBuffer = FitsInBytes<T, N> && TriviallyCopyable<T>;

    static_assert(Integral<u32>,          "u32 must satisfy Integral");
    static_assert(Integral<i64>,          "i64 must satisfy Integral");
    static_assert(!Integral<f32>,         "f32 must not satisfy Integral");
    static_assert(FloatingPoint<f64>,     "f64 must satisfy FloatingPoint");
    static_assert(Pointer<u32*>,          "u32* must satisfy Pointer");
    static_assert(!Pointer<u32>,          "u32 must not satisfy Pointer");
    static_assert(SameAs<u32, u32>,       "SameAs identity must hold");
    static_assert(!SameAs<u32, i32>,      "SameAs must distinguish sign");
    static_assert(TriviallyCopyable<u64>, "u64 must be trivially copyable");
    static_assert(POD<u32>,               "u32 must be POD");
    static_assert(StandardLayout<u32>,    "u32 must be standard layout");

} // namespace FoundationKit
