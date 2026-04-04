# FoundationKitCxxStl — Part 1: Base Types, Compiler Abstraction & Safety Primitives

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxStl` | **Header dir:** `FoundationKitCxxStl/Base/`

---

## 1.1 Fundamental Type Aliases — `Types.hpp`

FoundationKit does **not** rely on `<cstdint>` or `<cstddef>`. Instead, all primitive types are defined directly in `Types.hpp` using compiler-provided built-in types. This makes the library portable to bare-metal environments with no C runtime.

### Integer Types

| Alias    | Underlying Type            | Width    | Signed |
|----------|---------------------------|----------|--------|
| `i8`     | `signed char`             | 8-bit    | Yes    |
| `i16`    | `signed short`            | 16-bit   | Yes    |
| `i32`    | `signed int`              | 32-bit   | Yes    |
| `i64`    | `signed long long`        | 64-bit   | Yes    |
| `u8`     | `unsigned char`           | 8-bit    | No     |
| `u16`    | `unsigned short`          | 16-bit   | No     |
| `u32`    | `unsigned int`            | 32-bit   | No     |
| `u64`    | `unsigned long long`      | 64-bit   | No     |
| `usize`  | `__SIZE_TYPE__`           | arch     | No     |
| `isize`  | `__PTRDIFF_TYPE__`        | arch     | Yes    |
| `uptr`   | `__UINTPTR_TYPE__`        | arch     | No     |
| `byte`   | `unsigned char`           | 8-bit    | No     |

The `usize` and `isize` types are derived from compiler built-in `__SIZE_TYPE__` and `__PTRDIFF_TYPE__` macros, ensuring correctness on every supported architecture without including any system headers.

### 128-bit Types (Conditional)

```cpp
#if defined(__SIZEOF_INT128__)
    using u128 = unsigned __int128;
    using i128 = signed   __int128;
    #define FOUNDATIONKITCXXSTL_HAS_INT128
#endif
```

128-bit integers are enabled only when `__SIZEOF_INT128__` is available (GCC/Clang on 64-bit targets). Code that uses `u128`/`i128` must guard with `#ifdef FOUNDATIONKITCXXSTL_HAS_INT128`.

### Floating-Point Types

| Alias  | Underlying | Notes                       |
|--------|------------|-----------------------------|
| `f32`  | `float`    | IEEE 754 single-precision   |
| `f64`  | `double`   | IEEE 754 double-precision   |

### Size and Alignment Helpers

```cpp
template <typename T>
inline constexpr usize SizeOf = sizeof(T);

template <typename T>
inline constexpr usize AlignOf = alignof(T);
```

These `inline constexpr` variables behave identically to `sizeof`/`alignof` but can be used in template arguments and concept constraints without syntactic noise.

---

## 1.2 Compiler Abstraction — `Compiler.hpp`

This header establishes the **compiler detection layer** and defines cross-compiler attribute macros. All library code uses these macros instead of raw GCC/MSVC pragmas.

### Compiler Detection

```cpp
#if defined(__clang__)
    #define FOUNDATIONKITCXXSTL_COMPILER_CLANG
#elif defined(__GNUC__)
    #define FOUNDATIONKITCXXSTL_COMPILER_GCC
#elif defined(_MSC_VER)
    #define FOUNDATIONKITCXXSTL_COMPILER_MSVC
#endif
```

### Key Attribute Macros

| Macro                              | GCC/Clang Expansion             | MSVC Expansion       |
|------------------------------------|---------------------------------|----------------------|
| `FOUNDATIONKITCXXSTL_ALWAYS_INLINE`| `__attribute__((always_inline)) inline` | `__forceinline` |
| `FOUNDATIONKITCXXSTL_NOINLINE`     | `__attribute__((noinline))`     | `__declspec(noinline)` |
| `FOUNDATIONKITCXXSTL_PACKED`       | `__attribute__((packed))`       | `#pragma pack`       |
| `FOUNDATIONKITCXXSTL_UNREACHABLE`  | `__builtin_unreachable()`       | `__assume(0)`        |

**Rule:** Never write `__attribute__` or `__forceinline` directly in library code. Always use the macros above to maintain portability.

### Diagnostic Suppression

```cpp
FOUNDATIONKITCXXSTL_DIAGNOSTIC_PUSH
FOUNDATIONKITCXXSTL_DIAGNOSTIC_IGNORE("-Wsome-warning")
// ... code that triggers the warning ...
FOUNDATIONKITCXXSTL_DIAGNOSTIC_POP
```

These macros expand to `_Pragma("GCC diagnostic push/pop/ignored")` on GCC/Clang and `__pragma(warning(push/pop/disable:...))` on MSVC, enabling precise, scoped warning suppression.

---

## 1.3 Low-Level Compiler Builtins — `CompilerBuiltins.hpp`

Located in the `FoundationKitCxxStl::Base::CompilerBuiltins` namespace, this header wraps compiler intrinsics into portable inline functions. It is the **only place** where compiler built-ins should be called directly.

### Memory Operations

```cpp
// Copy n bytes from src to dst (non-overlapping).
void MemCpy(void* dst, const void* src, usize n);

// Move n bytes (handles overlapping regions).
void MemMove(void* dst, const void* src, usize n);

// Set n bytes in dst to value.
void MemSet(void* dst, byte value, usize n);

// Lexicographic comparison of two memory regions.
i32 MemCmp(const void* lhs, const void* rhs, usize n);

// Bitwise type-pun: reinterpret From as To without UB.
template <typename To, typename From>
To BitCast(const From& from);
```

All of these forward directly to `__builtin_memcpy`, `__builtin_memmove`, `__builtin_memset`, `__builtin_memcmp`, and `__builtin_bit_cast`, which are available in freestanding mode and optimised by the compiler into SIMD instructions when appropriate.

### Atomic Operations

All atomic operations take a raw pointer and an integer `order` (matching `__ATOMIC_RELAXED` etc.):

```cpp
template <typename T>
T    AtomicLoad  (const T* ptr, int order);
void AtomicStore (T* ptr, T val, int order);
T    AtomicExchange(T* ptr, T val, int order);

bool AtomicCompareExchange(T* ptr, T* expected, T desired,
                           bool weak, int success, int failure);

T AtomicFetchAdd(T* ptr, T val, int order);
T AtomicFetchSub(T* ptr, T val, int order);
T AtomicFetchAnd(T* ptr, T val, int order);
T AtomicFetchOr (T* ptr, T val, int order);
T AtomicFetchXor(T* ptr, T val, int order);

bool AtomicTestAndSet(volatile bool* ptr, int order);
void AtomicClear     (volatile bool* ptr, int order);
```

These wrap the GCC `__atomic_*` built-ins, providing the foundation for `Atomic<T>`, `SpinLock`, and all other synchronisation primitives.

### CPU Instructions

```cpp
void CpuPause();            // x86: PAUSE; ARM: YIELD
void MemoryBarrier();       // Full memory barrier (__sync_synchronize)
void MemoryBarrierLoad();   // Acquire barrier
void MemoryBarrierStore();  // Release barrier
```

`CpuPause()` is critical in spin-loops — it hints to the CPU's branch predictor that a spin-wait is occurring, which reduces power consumption and allows hyperthread sibling instructions to execute.

### Bit Manipulation

```cpp
i32 CountLeadingZeros(unsigned int);
i32 CountLeadingZerosL(unsigned long);
i32 CountLeadingZerosLL(unsigned long long);

i32 CountTrailingZeros(unsigned int);
i32 CountTrailingZerosL(unsigned long);
i32 CountTrailingZerosLL(unsigned long long);

i32 PopCount(unsigned int);
i32 PopCountL(unsigned long);
i32 PopCountLL(unsigned long long);
```

These wrap `__builtin_clz*`, `__builtin_ctz*`, and `__builtin_popcount*` respectively. The `Bit.hpp` header provides type-safe wrappers over these (`CountLeadingZeros<T>`, `PopCount<T>`, etc.) that dispatch to the correct variant based on `sizeof(T)`.

---

## 1.4 Runtime Safety Macros — `Bug.hpp`

FoundationKit uses two macros for runtime integrity checking, both defined in `Bug.hpp`:

### `FK_BUG_ON(condition, fmt, ...)`

```cpp
FK_BUG_ON(ptr == nullptr, "MemoryCopy: null destination pointer (size: {})", size);
```

**Semantics:** If `condition` evaluates to `true` at runtime, this macro:
1. Formats the diagnostic message using the `StaticStringBuilder` (stack only, no heap).
2. Calls `OslBug(message)` — the OS Layer's fatal abort function.
3. `OslBug` is declared `[[noreturn]]`, so the compiler knows execution cannot continue.

`FK_BUG_ON` should be used for **invariant violations** — states that must never occur under correct usage. Think of it as the freestanding equivalent of `assert()`, but with formatted messages and no dependency on `<cassert>`.

### `FK_WARN_ON(condition, fmt, ...)`

```cpp
FK_WARN_ON(stats.total_allocations == 0, "AllocationStats: query on empty stats");
```

**Semantics:** If `condition` is true, formats and logs a warning via `OslLog()` but **does not halt execution**. Use this for degraded but recoverable states.

### `FK_BUG(fmt, ...)` — Unconditional Fatal

```cpp
FK_BUG("MemoryRegion: corruption detected - magic mismatch ({:#x})", found_magic);
```

Equivalent to `FK_BUG_ON(true, ...)`. Used when a code path should be logically unreachable (e.g., inside a `default:` of an exhaustive `switch`).

### `FK_LOG_WARN(fmt, ...)` / `FK_LOG_INFO(fmt, ...)`

Emit formatted log messages via the OSL without aborting.

---

## 1.5 Concepts — `Meta/Concepts.hpp`

All template constraints in FoundationKit are expressed via C++20/23 concepts defined in this header. This eliminates SFINAE metaprogramming and produces human-readable compiler errors.

### Core Type Concepts

```cpp
template <typename T, typename U>
concept SameAs = __is_same(T, U);

template <typename T>
concept Integral = __is_integral(T);

template <typename T>
concept Signed = Integral<T> && (T(-1) < T(0));

template <typename T>
concept Unsigned = Integral<T> && !Signed<T>;

template <typename T>
concept FloatingPoint = __is_floating_point(T);

template <typename T>
concept Arithmetic = Integral<T> || FloatingPoint<T>;

template <typename T>
concept Pointer = __is_pointer(T);

template <typename T>
concept Enum = __is_enum(T);
```

These use compiler built-in type traits (`__is_same`, `__is_integral`, etc.) to avoid including `<type_traits>`, which is not available in freestanding environments.

### Iterator Concepts

```cpp
template <typename I>
concept InputIterator = requires(I i) {
    { *i };
    { ++i } -> SameAs<I&>;
    { i != i } -> SameAs<bool>;
};

template <typename I>
concept ForwardIterator = InputIterator<I>;

template <typename I>
concept BidirectionalIterator = ForwardIterator<I> && requires(I i) {
    { --i } -> SameAs<I&>;
};

template <typename I>
concept RandomAccessIterator = BidirectionalIterator<I> && requires(I i, isize n) {
    { i + n } -> SameAs<I>;
    { i - n } -> SameAs<I>;
    { i - i } -> SameAs<isize>;
};

template <typename O, typename T>
concept OutputIterator = requires(O o, T val) {
    { *o = val };
    { ++o };
};
```

### Lock Concepts

```cpp
template <typename L>
concept BasicLockable = requires(L l) {
    { l.Lock()   } -> SameAs<void>;
    { l.Unlock() } -> SameAs<void>;
};

template <typename L>
concept Lockable = BasicLockable<L> && requires(L l) {
    { l.TryLock() } -> SameAs<bool>;
};

template <typename L>
concept SharedLockable = Lockable<L> && requires(L l) {
    { l.LockShared()    } -> SameAs<void>;
    { l.TryLockShared() } -> SameAs<bool>;
    { l.UnlockShared()  } -> SameAs<void>;
};
```

### Type Trait Helpers

```cpp
template <typename T>
using RemoveRef = __remove_reference_t<T>;

template <typename T>
using RemoveCV = __remove_cv_t<T>;

template <typename T>
inline constexpr bool IsArray = __is_array(T);

template <typename Base, typename Derived>
inline constexpr bool BaseOf = __is_base_of(Base, Derived);

template <typename T>
inline constexpr bool TriviallyCopyable = __is_trivially_copyable(T);

template <typename T>
inline constexpr bool DefaultConstructible = __is_constructible(T);
```

---

## 1.6 Core Utilities — `Utility.hpp`

This header provides the minimal set of generic utilities analogous to `<utility>` from the standard library.

### `Move` and `Forward`

```cpp
template <typename T>
constexpr RemoveRef<T>&& Move(T&& t) noexcept;

template <typename T>
constexpr T&& Forward(RemoveRef<T>& t) noexcept;

template <typename T>
constexpr T&& Forward(RemoveRef<T>&& t) noexcept;
```

These provide perfect forwarding and move semantics without `<utility>`. They delegate to `static_cast` with the correct reference types.

### `Swap`

```cpp
template <typename T>
constexpr void Swap(T& a, T& b) noexcept;
```

Move-based swap. Used by `Algorithm.hpp`'s `Reverse`, `Rotate`, and `Sort`.

### In-Place Construction

```cpp
// Construct T at raw memory location 'ptr' with forwarded arguments.
template <typename T, typename... Args>
T* ConstructAt(void* ptr, Args&&... args) noexcept;

// Destroy T at 'ptr' without deallocating.
template <typename T>
void DestroyAt(T* ptr) noexcept;
```

`ConstructAt` uses placement new: `new(ptr) T(Forward<Args>(args)...)`. This is the standard freestanding way to construct objects in pre-allocated memory and is used throughout the container and memory systems.

### `InitializerList`

```cpp
template <typename T>
using InitializerList = std::initializer_list<T>;
```

`std::initializer_list` is special: it is a core language feature available in freestanding mode (it does not depend on any library header). FoundationKit aliases it as `InitializerList<T>` for namespace consistency.
