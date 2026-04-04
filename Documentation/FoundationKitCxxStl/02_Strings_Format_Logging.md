# FoundationKitCxxStl — Part 2: Strings, Formatting & Logging

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxStl` | **Header dir:** `FoundationKitCxxStl/Base/`

---

## 2.1 `StringView` — Non-Owning String Reference

`StringView` is a lightweight, non-owning reference to a character array. It never allocates memory. It is the primary string parameter type throughout FoundationKit — wherever a function accepts text, it accepts `StringView`.

### Construction

```cpp
StringView sv;                   // empty view (nullptr, 0)
StringView sv("hello");          // null-terminated C string
StringView sv("hello", 5);       // explicit pointer + length
StringView sv(str.Data(), str.Size()); // from any string
```

### Core Methods

```cpp
[[nodiscard]] const char* Data()  const noexcept;
[[nodiscard]] usize       Size()  const noexcept;
[[nodiscard]] bool        Empty() const noexcept;

char operator[](usize i) const noexcept;  // unchecked
```

### Substring Operations

```cpp
// Returns a view starting at 'offset', spanning 'count' chars (or to end).
StringView SubView(usize offset, usize count = npos) const noexcept;

// Trim whitespace from both ends; returns new view.
StringView Trim() const noexcept;

// Search for character; returns npos if not found.
usize Find(char c, usize start = 0) const noexcept;
```

### String Predicates

```cpp
bool StartsWith(StringView prefix) const noexcept;
bool EndsWith(StringView suffix)   const noexcept;
bool Contains(StringView sub)      const noexcept;

// Lexicographic comparison (same semantics as strcmp).
i32 Compare(StringView other) const noexcept;
bool operator==(StringView other) const noexcept;
bool operator!=(StringView other) const noexcept;
```

### Key Design Notes

- **No heap allocation.** `StringView` is always `const char* + usize`. It is trivially copyable and should be passed by value.
- **Null-termination not guaranteed.** Callers must not pass `StringView::Data()` to C APIs that expect null-terminated strings without checking.
- **`npos = static_cast<usize>(-1)`** is the sentinel for "not found" / "until end", matching the standard library convention.

---

## 2.2 `String` — Owning Dynamic String (with SSO)

`String` is the heap-allocated, owning string class. It implements **Small String Optimisation (SSO)**: strings shorter than an internal threshold are stored in a stack buffer, avoiding heap allocation entirely.

### SSO Layout

The `String` union stores either:
- **Inline:** `char m_inline_data[23]` + `u8 m_inline_size` (total 24 bytes on 64-bit)
- **Heap:** `char* m_data` + `usize m_size` + `usize m_capacity`

The MSB of the last byte of the union indicates which mode is active.

### Construction

```cpp
String s;                           // empty
String s("hello");                  // from C string
String s("hello", 5);               // from pointer + length
String s(sv);                       // from StringView
String s(other_string);             // copy (allocates if large)
String s(Move(other_string));       // move (O(1), no allocation)
```

### Core Interface

```cpp
[[nodiscard]] const char* Data()     const noexcept;
[[nodiscard]] char*       Data()           noexcept;
[[nodiscard]] usize       Size()     const noexcept;
[[nodiscard]] usize       Capacity() const noexcept;
[[nodiscard]] bool        Empty()    const noexcept;

// Implicit conversion to non-owning view.
operator StringView() const noexcept;
```

### Mutation

```cpp
// Append a character, StringView, or another String.
bool Append(char c)          noexcept;
bool Append(StringView sv)   noexcept;
bool Append(const String& s) noexcept;

// Reserve at least 'cap' bytes (re-allocates if necessary).
bool Reserve(usize cap) noexcept;

// Resize to exactly 'n' characters (fills with '\0').
bool Resize(usize n) noexcept;

// Clear content; does NOT release heap memory.
void Clear() noexcept;
```

### Allocator Customisation

`String` is templated on an allocator but the default is `FoundationKitMemory::AnyAllocator`, which defers to the **global allocator**. A custom allocator is passed at construction:

```cpp
template <FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
class String { ... };

// With custom allocator:
String<BumpAllocator> s(my_bump_alloc);
```

---

## 2.3 Format System — `Format.hpp`

FoundationKit implements its own type-safe formatting engine for use in logging, diagnostics, and string construction. It is strictly freestanding — no `printf`, no `<format>`.

### `FormatSpec`

```cpp
struct FormatSpec {
    char fill      = ' ';   // fill character for width padding
    char align     = '<';   // '<', '>', '^'
    char sign      = '-';   // '-', '+', ' '
    char type      = 0;     // 'd', 'x', 'X', 'o', 'b', 'f', 'e', 's', 'p', '#'
    i32  width     = -1;    // minimum field width (-1 = none)
    i32  precision = -1;    // digits after decimal / max string chars (-1 = none)
    bool alt_form  = false; // '#' prefix (0x, 0b, 0o)
};
```

`FormatSpec` is parsed from the mini-language inside `{}` in a format string. Example: `{:#016x}` → fill `'0'`, width `16`, alt form, hex type.

### `Formatter<T>` — Specialisation Protocol

Every type that wishes to be formattable must provide (or have provided for it) a `Formatter` specialisation:

```cpp
template <>
struct Formatter<MyType> {
    template <typename Sink>
    void Format(Sink& sb, const MyType& value, const FormatSpec& spec = {});
};
```

The `Sink` can be any type with `Append(char)` and `Append(const char*, usize)` methods. This includes both `StaticStringBuilder` (stack) and `StringBuilder` (heap-backed).

### Built-in Formatter Specialisations

| Type                        | Format Behaviour                                      |
|-----------------------------|-------------------------------------------------------|
| `bool`                      | `"true"` / `"false"`, or `1`/`0` with numeric type   |
| `i8`, `i16`, `i32`, `i64`  | Decimal by default; `x/X/o/b` for hex/octal/binary   |
| `u8`, `u16`, `u32`, `u64`  | Same as signed                                        |
| `f32`, `f64`                | Decimal with precision; `e` for scientific notation   |
| `const char*`, `StringView` | String output; precision limits length                |
| `void*`, `T*`               | Pointer hexadecimal (`0x...`)                         |
| `char`                      | Single character                                      |

**Example format strings:**

```cpp
// D is decimal (default), x is hex, #x is 0x-prefixed hex
"{}"       → Formatter<T>::Format with empty spec
"{:x}"     → hex, lowercase
"{:#010x}" → 0x-prefixed, zero-padded, 10 wide
"{:.4f}"   → float with 4 decimal places
"{:>20}"   → right-aligned in 20-wide field
```

### How Format Dispatch Works

The engine uses a variadic template unpacked at compile time. The format string is scanned character by character:

1. Literal characters are appended directly.
2. `{` begins a format specifier; `FormatSpec` is parsed.
3. The corresponding argument is formatted using `Formatter<ArgType>::Format(sink, arg, spec)`.
4. Repeat until all arguments are consumed.

This is entirely resolved at compile time for the argument types — no runtime type information or vtable dispatch.

---

## 2.4 `StaticStringBuilder` — Stack-Only String Building

```cpp
template <usize Capacity = 512>
class StaticStringBuilder {
public:
    void Append(char c) noexcept;
    void Append(const char* str, usize len) noexcept;

    template <typename... Args>
    void Format(StringView fmt, Args&&... args) noexcept;

    [[nodiscard]] StringView View() const noexcept;
    [[nodiscard]] const char* CStr() const noexcept;
    [[nodiscard]] usize Size() const noexcept;
    void Clear() noexcept;
};
```

`StaticStringBuilder` is the **heap-free** string builder. It allocates its buffer on the stack and truncates silently if `Capacity` is exceeded. It is used exclusively in early-boot and ISR contexts where heap access is forbidden.

**Key Rule:** `StaticStringBuilder` is used internally by `FK_BUG_ON` and `FK_WARN_ON` to format the diagnostic message before calling `OslBug` / `OslLog`, precisely because those paths must work before the global allocator is initialised.

---

## 2.5 `StringBuilder` — Heap-Backed String Building

```cpp
class StringBuilder {
public:
    explicit StringBuilder(FoundationKitMemory::AnyAllocator alloc = {});

    void Append(char c);
    void Append(StringView sv);

    template <typename... Args>
    void AppendFormat(StringView fmt, Args&&... args);

    [[nodiscard]] String Build() &&;    // moves internal buffer into a String
    [[nodiscard]] StringView View() const noexcept;

    void Clear() noexcept;
    [[nodiscard]] usize Size() const noexcept;
};
```

`StringBuilder` backs its buffer with the allocator. Typical usage:

```cpp
StringBuilder sb(my_allocator);
sb.AppendFormat("Process {} started at {:#x}", pid, entry_point);
String result = Move(sb).Build();
```

---

## 2.6 Logging System — `Logger.hpp`

The logger is the public API for diagnostic output. It sits on top of the format system and the OSL output functions.

### Log Levels

```cpp
enum class LogLevel : u8 {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};
```

### The `FK_LOG_*` Macros

```cpp
FK_LOG_TRACE   ("format {}", arg)
FK_LOG_DEBUG   ("format {}", arg)
FK_LOG_INFO    ("format {}", arg)
FK_LOG_WARN    ("format {}", arg)
FK_LOG_ERROR   ("format {}", arg)
FK_LOG_CRITICAL("format {}", arg)
```

These macros expand to calls to `FoundationKitCxxStl::Log(level, fmt, args...)`. Internally, the log function:

1. Builds the message on a `StaticStringBuilder<512>` (stack-only, no allocation).
2. Prepends the log level tag (e.g., `[WARN] `).
3. Calls `OslLog(message.CStr())`.

Since the `StaticStringBuilder` is stack-based, logging is safe in interrupt handlers, early boot, and pre-allocator contexts.

### Runtime Level Filtering

A global `Atomic<LogLevel>` stores the minimum log level. Messages below this level are discarded before formatting. Changing the level is thread-safe:

```cpp
FoundationKitCxxStl::SetLogLevel(LogLevel::Warn);
```

---

## 2.7 `NumericLimits<T>` — Arithmetic Bounds

`NumericLimits<T>` provides compile-time type properties, analogous to `std::numeric_limits`, but without `<limits>`.

```cpp
NumericLimits<u64>::Min()  // 0
NumericLimits<u64>::Max()  // 2^64 - 1
NumericLimits<i32>::Min()  // -2147483648
NumericLimits<i32>::Max()  // 2147483647
NumericLimits<f32>::Min()  // 1.17549435e-38F (smallest positive normal)
NumericLimits<f32>::Max()  // 3.40282347e+38F
```

Specialisations exist for all `i8`–`i64`, `u8`–`u64`, `u128`/`i128` (conditional), `f32`, and `f64`. The primary template has `IsSpecialized = false` so generic code can detect unsupported types:

```cpp
static_assert(NumericLimits<u32>::IsSpecialized, "Need numeric bounds for this type");
```
