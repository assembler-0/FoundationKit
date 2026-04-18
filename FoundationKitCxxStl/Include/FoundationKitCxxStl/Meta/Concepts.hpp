#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitCxxStl {

    // --- Core Language Concepts ---

    /// @brief Checks if T and U are the same type.
    template <typename T, typename U>
    concept SameAs = __is_same(T, U);

    /// @brief Checks if Derived is derived from Base.
    template <typename Derived, typename Base>
    concept DerivedFrom = __is_base_of(Base, Derived) &&
                         __is_convertible_to(const volatile Derived*, const volatile Base*);

    /// @brief Checks if From is convertible to To.
    template <typename From, typename To>
    concept ConvertibleTo = __is_convertible_to(From, To);

    /// @brief Checks if Base is a base class of Derived.
    template <typename Base, typename Derived>
    concept BaseOf = __is_base_of(Base, Derived);

    // --- Primary Type Categories ---

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
    template <typename T> concept NullPointer      = SameAs<T, nullptr_t>;
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

    // --- Meta-programming Utilities ---

    template <typename T>
    struct RemoveCvRef {
        using Type = __remove_cvref(T);
    };

    template <typename T>
    using Unqualified = RemoveCvRef<T>::Type;

    template <typename T>
    struct RemoveReference      { using Type = T; };
    template <typename T> struct RemoveReference<T&>  { using Type = T; };
    template <typename T> struct RemoveReference<T&&> { using Type = T; };

    template <typename T>
    using RemoveReferenceT =  RemoveReference<T>::Type;

    /// @brief Declares an object of type T in an unevaluated context.
    /// @note This function has NO implementation. It is only for type traits and concepts.
    template <typename T>
    T&& DeclVal() noexcept;

    // --- Construction & Destruction ---

    template <typename T>
    concept Destructible = (ObjectType<T> || Reference<T>) && requires(T& t) {
        { t.~T() } noexcept;
    };

    template <typename T>
    concept TriviallyDestructible = Destructible<T> && __is_trivially_destructible(T);

    template <typename T>
    concept NothrowDestructible = Destructible<T> && requires(T& t) {
        { t.~T() } noexcept;
    };

    template <typename T, typename... Args>
    concept ConstructibleFrom = Destructible<T> && __is_constructible(T, Args...);

    template <typename T>
    concept DefaultConstructible = ConstructibleFrom<T>;

    template <typename T>
    concept TriviallyDefaultConstructible = DefaultConstructible<T> && __is_trivially_constructible(T);

    template <typename T>
    concept NothrowDefaultConstructible = DefaultConstructible<T> && __is_nothrow_constructible(T);

    template <typename T>
    concept MoveConstructible = ConstructibleFrom<T, T> && ConvertibleTo<T, T>;

    template <typename T>
    concept TriviallyMoveConstructible = MoveConstructible<T> && __is_trivially_constructible(T, T);

    template <typename T>
    concept NothrowMoveConstructible = MoveConstructible<T> && __is_nothrow_constructible(T, T);

    template <typename T>
    concept CopyConstructible = MoveConstructible<T> &&
                               ConstructibleFrom<T, T&> && ConvertibleTo<T&, T> &&
                               ConstructibleFrom<T, const T&> && ConvertibleTo<const T&, T> &&
                               ConstructibleFrom<T, const T> && ConvertibleTo<const T, T>;

    template <typename T>
    concept TriviallyCopyConstructible = CopyConstructible<T> &&
                                        __is_trivially_constructible(T, const T&);

    template <typename T>
    concept NothrowCopyConstructible = CopyConstructible<T> &&
                                      __is_nothrow_constructible(T, const T&);

    // --- Assignment ---

    template <typename LHS, typename RHS>
    concept AssignableFrom =
        LValueReference<LHS> &&
        requires(LHS lhs, RHS&& rhs) {
            { lhs = static_cast<RHS&&>(rhs) } -> SameAs<LHS>;
        };

    template <typename T>
    concept CopyAssignable = AssignableFrom<T&, const T&>;

    template <typename T>
    concept TriviallyCopyAssignable = CopyAssignable<T> && __is_trivially_assignable(T&, const T&);

    template <typename T>
    concept NothrowCopyAssignable = CopyAssignable<T> && __is_nothrow_assignable(T&, const T&);

    template <typename T>
    concept MoveAssignable = AssignableFrom<T&, T>;

    template <typename T>
    concept TriviallyMoveAssignable = MoveAssignable<T> && __is_trivially_assignable(T&, T);

    template <typename T>
    concept NothrowMoveAssignable = MoveAssignable<T> && __is_nothrow_assignable(T&, T);

    // --- Common Concepts ---

    template <typename T>
    void Swap(T& a, T& b) noexcept; // Forward declaration

    template <typename T>
    concept Swappable = requires(T& a, T& b) {
        { Swap(a, b) } noexcept;
    };

    template <typename T, typename U>
    concept SwappableWith =
        requires(T&& t, U&& u) {
            { Swap(static_cast<T&&>(t), static_cast<T&&>(t)) } noexcept;
            { Swap(static_cast<U&&>(u), static_cast<U&&>(u)) } noexcept;
            { Swap(static_cast<T&&>(t), static_cast<U&&>(u)) } noexcept;
            { Swap(static_cast<U&&>(u), static_cast<T&&>(t)) } noexcept;
        };

    // --- Comparison Concepts ---

    template <typename T>
    concept BooleanTestable = ConvertibleTo<T, bool> &&
        requires(T&& t) {
            { !static_cast<T&&>(t) } -> ConvertibleTo<bool>;
        };

    template <typename T, typename U>
    concept WeaklyEqualityComparableWith =
        requires(const Unqualified<T>& t, const Unqualified<U>& u) {
            { t == u } -> BooleanTestable;
            { t != u } -> BooleanTestable;
            { u == t } -> BooleanTestable;
            { u != t } -> BooleanTestable;
        };

    template <typename T>
    concept EqualityComparable = WeaklyEqualityComparableWith<T, T>;

    template <typename T, typename U>
    concept EqualityComparableWith =
        EqualityComparable<T> &&
        EqualityComparable<U> &&
        WeaklyEqualityComparableWith<T, U>;

    template <typename T>
    concept TotallyOrdered = EqualityComparable<T> &&
        requires(const Unqualified<T>& a, const Unqualified<T>& b) {
            { a <  b } -> BooleanTestable;
            { a >  b } -> BooleanTestable;
            { a <= b } -> BooleanTestable;
            { a >= b } -> BooleanTestable;
        };

    template <typename T>
    concept ThreeWayComparable = requires(const T& a, const T& b) {
        { a <=> b };
    };

    // --- Object Concepts ---

    template <typename T>
    concept Movable = ObjectType<T> && MoveConstructible<T> &&
                     AssignableFrom<T&, T> && Swappable<T>;

    template <typename T>
    concept Copyable = Movable<T> && CopyConstructible<T> &&
                      AssignableFrom<T&, T&> &&
                      AssignableFrom<T&, const T&> &&
                      AssignableFrom<T&, const T>;

    template <typename T>
    concept TriviallyCopyable = Copyable<T> && __is_trivially_copyable(T);
    
    template <typename T>
    concept Semiregular = Copyable<T> && DefaultConstructible<T>;

    template <typename T>
    concept Regular = Semiregular<T> && EqualityComparable<T>;

    // --- Callable Concepts ---

    template <typename F, typename... Args>
    concept Invocable = requires(F&& f, Args&&... args) {
        static_cast<F&&>(f)(static_cast<Args&&>(args)...);
    };

    template <typename F, typename... Args>
    concept RegularInvocable = Invocable<F, Args...>;

    template <typename F, typename... Args>
    concept Predicate = RegularInvocable<F, Args...> &&
                       BooleanTestable<decltype(DeclVal<F>()(DeclVal<Args>()...))>;

    template <typename R, typename T, typename U>
    concept Relation = Predicate<R, T, T> && Predicate<R, U, U> &&
                      Predicate<R, T, U> && Predicate<R, U, T>;

    template <typename R, typename T, typename U>
    concept StrictWeakOrder = Relation<R, T, U>;

    // --- Iterator Concepts ---

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

    // --- Range Concepts ---

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

    // --- Miscellaneous ---

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
    using MakeUnsignedT = MakeUnsigned<T>::Type;

    template <typename T, usize N>
    concept FitsInBytes = sizeof(T) <= N;

    template <typename T, usize N>
    concept AlignmentAtMost = alignof(T) <= N;

    template <typename T>
    concept StandardLayout = __is_standard_layout(T);

    template <typename T>
    concept POD = __is_trivial(T) && StandardLayout<T>;

    template <typename T, usize N>
    concept StorableInBuffer = FitsInBytes<T, N> && TriviallyCopyable<T>;

    // --- Internal Validation ---

    static_assert(Integral<u32>,          "u32 must satisfy Integral");
    static_assert(Integral<i64>,          "i64 must satisfy Integral");
    static_assert(!Integral<f32>,         "f32 must not satisfy Integral");
    static_assert(FloatingPoint<f64>,     "f64 must satisfy FloatingPoint");
    static_assert(Pointer<u32*>,          "u32* must satisfy Pointer");
    static_assert(!Pointer<u32>,          "u32 must not satisfy Pointer");
    static_assert(SameAs<u32, u32>,       "SameAs identity must hold");
    static_assert(!SameAs<u32, i32>,      "SameAs must distinguish sign");
    static_assert(TriviallyCopyable<u64>, "u64 must satisfy TriviallyCopyable");
    static_assert(TriviallyMoveConstructible<u32>, "u32 must satisfy TriviallyMoveConstructible");
    static_assert(TriviallyMoveAssignable<u32>, "u32 must satisfy TriviallyMoveAssignable");
    static_assert(StandardLayout<u32>,    "u32 must be standard layout");

} // namespace FoundationKitCxxStl
