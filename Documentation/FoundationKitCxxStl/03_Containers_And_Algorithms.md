# FoundationKitCxxStl — Part 3: Core Containers & Value Wrappers

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxStl` | **Header dir:** `FoundationKitCxxStl/Base/`

---

## 3.1 `Optional<T>` — Nullable Value Wrapper

`Optional<T>` represents a value that may or may not be present. It is the replacement for null pointers and boolean-plus-output-arg patterns.

### Storage

```cpp
template <typename T>
class Optional {
    alignas(T) byte m_storage[sizeof(T)];
    bool            m_has_value;
};
```

No heap allocation. The value is stored inline using aligned byte storage and placement new.

### Construction

```cpp
Optional<i32> empty;               // no value
Optional<i32> val(42);             // has value
Optional<i32> val = NullOpt;       // explicit empty (NullOpt is a global tag)

// In-place construction (avoids copy):
Optional<MyType> opt(InPlace, arg1, arg2);
```

### Access

```cpp
// True if value present
explicit operator bool() const noexcept;
bool HasValue() const noexcept;

// Checked: panics via FK_BUG_ON if empty
T&       Value()       &;
const T& Value() const &;
T&&      Value()      &&;

// Unchecked (UB if empty - use only when you've verified HasValue())
T*       operator->()       noexcept;
const T* operator->() const noexcept;
T&       operator*()        noexcept;
const T& operator*()  const noexcept;

// Fallback
T ValueOr(T&& fallback) const noexcept;
```

### Monadic Operations

```cpp
// Apply f(value) if present; returns Optional<ReturnTypeOfF>
template <typename F>
auto AndThen(F&& f) && noexcept;

// Returns *this if present; otherwise returns other
Optional<T> OrElse(Optional<T> other) const noexcept;
```

### Typical Usage Patterns

```cpp
// Return from a function that may fail:
Optional<Node*> FindNode(u64 id) { ... }

auto node = FindNode(42);
if (!node) { return; }
node->DoWork();

// With ValueOr for safe defaults:
usize cap = GetMaybeCapacity().ValueOr(4096);
```

---

## 3.2 `Expected<T, E>` — Error-Carrying Result Type

`Expected<T, E>` holds either a value of type `T` (success) or an error of type `E` (failure). It is used instead of `Optional` when the **reason** for failure matters. It is analogous to `std::expected` (C++23).

### Construction

```cpp
Expected<i32, MemoryError> ok_val(42);
Expected<i32, MemoryError> err_val(MemoryError::OutOfMemory);

// Factory helpers:
Expected<i32, MemoryError>::Ok(42);
Expected<i32, MemoryError>::Err(MemoryError::OutOfMemory);
```

### Access

```cpp
bool         HasValue() const noexcept;
explicit operator bool() const noexcept;  // true = success

T&       Value() &;          // panics if error
const T& Value() const&;
T&&      Value() &&;

E&       Error() &;          // panics if value
const E& Error() const&;
```

### Chaining

```cpp
// Returns Expected<U, E> where U is the return type of f.
// If *this is an error, propagates the error unchanged.
template <typename F>
auto Transform(F&& f) &&;

// Returns *this if ok; otherwise calls f(error) to produce a recovery value.
template <typename F>
Expected<T, E> OrElse(F&& f) &&;
```

### Canonical Usage

```cpp
Expected<UniquePtr<Thread>, OsError> SpawnThread(ThreadFunc f) {
    auto stack = AllocateStack();
    if (!stack) return OsError::OutOfMemory;     // propagate error
    return MakeUnique<Thread>(stack, f);          // return success
}

auto result = SpawnThread(my_func);
if (!result) {
    FK_LOG_ERROR("Thread spawn failed: {}", result.Error());
    return;
}
result.Value()->Start();
```

---

## 3.3 `Variant<Ts...>` — Type-Safe Union

`Variant` is a type-safe discriminated union. It stores one value of exactly one of the listed types at a time. The active type is tracked by an index.

### Storage

```cpp
template <typename... Ts>
class Variant {
    // Correctly aligned for the largest type in Ts...
    alignas(/* max align of Ts... */) byte m_storage[/* max size of Ts... */];
    u8 m_index;  // 0xFF = valueless_by_exception (actually: uninitialised)
};
```

### Construction

```cpp
Variant<i32, f64, StringView> v(42);        // holds i32
Variant<i32, f64, StringView> v(3.14);      // holds f64
Variant<i32, f64, StringView> v = InPlace<StringView>{}, "hello");
```

### Access via `Get<T>` / `Get<I>`

```cpp
// By type — panics via FK_BUG_ON if wrong type active
i32& val = Get<i32>(v);

// By index — same
auto& val = Get<0>(v);

// Non-panicking pointer access (returns nullptr if wrong type)
i32* p = GetIf<i32>(&v);   // nullptr if v doesn't hold i32
```

### `HoldsAlternative<T>`

```cpp
if (HoldsAlternative<f64>(v)) {
    FK_LOG_DEBUG("Variant holds a double: {}", Get<f64>(v));
}
```

### Visiting

```cpp
// Visit calls the appropriate overload of visitor based on active type.
Visit([](i32 val){ FK_LOG_INFO("int: {}", val); },
      [](f64 val){ FK_LOG_INFO("double: {}", val); },
      [](StringView s){ FK_LOG_INFO("str: {}", s); },
      v);
```

`Visit` is implemented with a compile-time function pointer table (no `dynamic_cast`, no `typeid`).

---

## 3.4 `Pair<F, S>` — Two-Element Tuple

```cpp
template <typename F, typename S>
struct Pair {
    F first;
    S second;
};

// Construction helper (avoids explicit template args):
auto p = MakePair(42, StringView("hello"));
```

`Pair` is an aggregate: no constructors beyond the aggregate default. Supports structured bindings:

```cpp
auto [key, value] = SomeMap.Get(id).Value();
```

---

## 3.5 `Array<T, N>` — Fixed-Size Array Container

`Array<T, N>` wraps a raw C-style array `T m_data[N]` with a safe, iterable interface. Size is a compile-time constant. No heap allocation.

### Interface

```cpp
template <typename T, usize N>
class Array {
public:
    // Checked element access (FK_BUG_ON out of bounds)
    T&       At(usize i)       noexcept;
    const T& At(usize i) const noexcept;

    // Unchecked access
    T&       operator[](usize i)       noexcept;
    const T& operator[](usize i) const noexcept;

    // Iterators
    T*       begin() noexcept;
    T*       end()   noexcept;
    const T* begin() const noexcept;
    const T* end()   const noexcept;

    // Queries
    [[nodiscard]] static constexpr usize Size()  noexcept { return N; }
    [[nodiscard]] static constexpr bool  Empty() noexcept { return N == 0; }

    // Raw pointer
    [[nodiscard]] T*       Data()       noexcept;
    [[nodiscard]] const T* Data() const noexcept;
};
```

### Aggregate Initialisation

```cpp
Array<i32, 4> arr = {1, 2, 3, 4};
Array<i32, 4> arr{};  // zero-initialised
```

### Static Assertions

```cpp
static_assert(sizeof(Array<i32, 4>) == 4 * sizeof(i32));
```

---

## 3.6 `Span<T>` — Non-Owning Contiguous View

`Span<T>` is a reference to a contiguous range of `T` objects. It does not own the memory. It is the universal function parameter type for "give me access to some elements":

```cpp
template <typename T>
class Span {
    T*    m_data;
    usize m_size;
public:
    constexpr Span() noexcept;
    constexpr Span(T* data, usize size) noexcept;

    template <usize N>
    constexpr Span(T (&arr)[N]) noexcept;  // from C array

    template <usize N>
    constexpr Span(Array<T, N>& arr) noexcept;  // from Array

    [[nodiscard]] T*    Data() const noexcept;
    [[nodiscard]] usize Size() const noexcept;
    [[nodiscard]] bool  Empty() const noexcept;

    T& operator[](usize i) const noexcept;  // unchecked

    // Sub-span:
    Span<T> SubSpan(usize offset, usize count = npos) const noexcept;
    Span<T> First(usize count) const noexcept;
    Span<T> Last(usize count)  const noexcept;

    T* begin() const noexcept;
    T* end()   const noexcept;
};
```

**Usage pattern:**

```cpp
void ZeroRegion(Span<byte> region) {
    for (auto& b : region) b = 0;
}

byte buffer[4096];
ZeroRegion(Span<byte>(buffer, sizeof(buffer)));
```

---

## 3.7 `Vector<T, Alloc>` — Dynamic Array

`Vector` is the heap-backed dynamic array. It manages a contiguous memory block that grows exponentially (factor ×2) when capacity is exceeded.

### Template Parameters

```cpp
template <typename T,
          FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
class Vector;
```

The default allocator is `AnyAllocator` (delegates to the global allocator). Override to use a custom allocator:

```cpp
Vector<u32, BumpAllocator> v(my_bump);
```

### Core Interface

```cpp
// Construction
Vector();
explicit Vector(Alloc alloc);

// Element access (checked)
T&       At(usize i);
const T& At(usize i) const;
T&       operator[](usize i);
const T& operator[](usize i) const;

// Front / back (panics if empty)
T& Front(); const T& Front() const;
T& Back();  const T& Back()  const;

// Mutation
template <typename... Args>
bool PushBack(Args&&... args);    // emplace at end; returns false on OOM

void PopBack();                   // remove last (panics if empty)

template <typename... Args>
bool Insert(usize pos, Args&&... args);

bool Remove(usize pos);

// Capacity management
bool Reserve(usize capacity);
bool Resize(usize size);
bool Resize(usize size, const T& value);

void Clear() noexcept;
void ShrinkToFit() noexcept;

// Queries
[[nodiscard]] usize Size()     const noexcept;
[[nodiscard]] usize Capacity() const noexcept;
[[nodiscard]] bool  Empty()    const noexcept;
[[nodiscard]] T*    Data()           noexcept;
[[nodiscard]] const T* Data()  const noexcept;

// Iterators
T* begin(); T* end();
const T* begin() const; const T* end() const;
```

### Growth Policy

When `PushBack` would exceed capacity:
1. `new_capacity = max(1, old_capacity * 2)`
2. Allocate new block via allocator.
3. Move all existing elements into the new block.
4. Deallocate old block.
5. Update internal pointers.

If `Reserve` or allocation fails (returns `nullptr`), `PushBack` returns `false` — the element is **not** inserted. This makes all mutations explicitly fallible, consistent with freestanding design philosophy.

---

## 3.8 `Hash` — FNV-1a Hash Function

```cpp
struct Hash {
    static constexpr u64 OffsetBasis = 0xcbf29ce484222325ULL;
    static constexpr u64 Prime       = 0x100000001b3ULL;

    template <typename T>
    requires Integral<T> || Enum<T>
    [[nodiscard]] constexpr u64 operator()(T value) const noexcept;

    [[nodiscard]] constexpr u64 operator()(const void* ptr) const noexcept;
    [[nodiscard]] constexpr u64 operator()(StringView view) const noexcept;
    [[nodiscard]] constexpr u64 operator()(const char* str) const noexcept;
};
```

FNV-1a is chosen for its simplicity and good distribution on small keys (common in kernel/embedded code). It processes the input byte-by-byte:

```
hash = OffsetBasis
for each byte b:
    hash ^= b
    hash *= Prime
```

The `Hash` struct is the default hash function for `HashMap<K, V>`.

---

## 3.9 `Bit.hpp` — Portable Bit Manipulation

The `Bit.hpp` header exposes type-safe wrappers for low-level bit operations, all `constexpr` and dispatching to `CompilerBuiltins` based on `sizeof(T)`:

```cpp
// Reinterpret bytes of From as To (no UB, no type-pun aliasing violations)
template <typename To, typename From>
[[nodiscard]] constexpr To BitCast(const From& from) noexcept;

// Count consecutive zero bits from MSB
template <Unsigned T>
[[nodiscard]] constexpr i32 CountLeadingZeros(T value) noexcept;

// Count consecutive zero bits from LSB
template <Unsigned T>
[[nodiscard]] constexpr i32 CountTrailingZeros(T value) noexcept;

// Count set bits
template <Unsigned T>
[[nodiscard]] constexpr i32 PopCount(T value) noexcept;

// True iff value is exactly a power of two
template <Unsigned T>
[[nodiscard]] constexpr bool IsPowerOfTwo(T value) noexcept;

// Circular shift left/right (safe, no UB on shift >= width)
template <Unsigned T>
[[nodiscard]] constexpr T RotateLeft (T value, i32 shift) noexcept;

template <Unsigned T>
[[nodiscard]] constexpr T RotateRight(T value, i32 shift) noexcept;
```

**`BitCast` is the freestanding alternative to `memcpy`-based type punning.** It correctly traces through placement new lifetime constraints and tools like UBSan.

---

## 3.10 Algorithm Library — `Algorithm.hpp`

Provides the standard algorithm library for freestanding code. All algorithms are templated on iterator concepts defined in `Concepts.hpp`.

### Search

```cpp
template <InputIterator I, typename T>
I Find(I first, I last, const T& value);

template <InputIterator I, typename Pred>
I FindIf(I first, I last, Pred pred);

template <ForwardIterator I, typename T, typename Comp = Less>
I LowerBound(I first, I last, const T& value, Comp comp = Comp{});

template <ForwardIterator I, typename T, typename Comp = Less>
I UpperBound(I first, I last, const T& value, Comp comp = Comp{});

template <ForwardIterator I, typename T, typename Comp = Less>
bool BinarySearch(I first, I last, const T& value, Comp comp = Comp{});
```

### Predicates

```cpp
template <InputIterator I, typename Pred>
bool AllOf (I first, I last, Pred pred);

template <InputIterator I, typename Pred>
bool AnyOf (I first, I last, Pred pred);

template <InputIterator I, typename Pred>
bool NoneOf(I first, I last, Pred pred);

template <InputIterator I, typename T>
isize Count  (I first, I last, const T& value);

template <InputIterator I, typename Pred>
isize CountIf(I first, I last, Pred pred);
```

### Transformation

```cpp
template <InputIterator I, typename Func>
Func ForEach(I first, I last, Func f);

template <InputIterator I, typename O, typename Func>
O Transform(I first, I last, O result, Func f);

template <InputIterator I, typename T, typename BinaryOp = Plus>
T Accumulate(I first, I last, T init, BinaryOp op = BinaryOp{});

template <InputIterator I, typename O>
O Copy(I first, I last, O result);

template <ForwardIterator I, typename T>
void Fill(I first, I last, const T& value);
```

### Reordering & Modification

```cpp
template <BidirectionalIterator I>
void Reverse(I first, I last);

template <ForwardIterator I>
I Rotate(I first, I n_first, I last);

template <ForwardIterator I>
I Unique(I first, I last);

template <ForwardIterator I, typename Pred>
I RemoveIf(I first, I last, Pred pred);
```

### Sort (`Sort`)

```cpp
template <RandomAccessIterator I, typename Comp = Less>
void Sort(I first, I last, Comp comp = Comp{});
```

Implements **Introsort**: hybrid of quicksort (median-of-three partition), heapsort as a depth-limit fallback (prevents O(n²)), and insertion sort for small sub-ranges (≤16 elements). Worst-case: **O(n log n)**. Average-case: **O(n log n)**. In-place, no heap allocation.

### Comparators

```cpp
struct Less {
    template <typename T, typename U>
    constexpr bool operator()(const T& lhs, const U& rhs) const { return lhs < rhs; }
};

struct Plus {
    template <typename T, typename U>
    constexpr auto operator()(const T& lhs, const U& rhs) const { return lhs + rhs; }
};
```

### `Min`, `Max`, `Clamp`

```cpp
template <typename T> constexpr const T& Min(const T& a, const T& b);
template <typename T> constexpr T        Min(InitializerList<T> list);
template <typename T> constexpr const T& Max(const T& a, const T& b);
template <typename T> constexpr T        Max(InitializerList<T> list);
template <typename T> constexpr const T& Clamp(const T& v, const T& lo, const T& hi);
```

---

## 3.11 `CommandLine` — Kernel Command Line Parser

```cpp
class CommandLine {
public:
    explicit CommandLine(StringView raw);

    // Check presence of flag (e.g., "debug" or "--force")
    [[nodiscard]] bool HasFlag(StringView name) const noexcept;

    // Get option value (e.g., "log_level=3" → "3")
    [[nodiscard]] Optional<StringView> GetOption(StringView name) const noexcept;

    // Get argument by position
    [[nodiscard]] Optional<StringView> GetArgument(usize index) const noexcept;

    [[nodiscard]] usize ArgumentCount() const noexcept;
};
```

`CommandLine` takes the raw multiboot/UEFI command line string, splits it on spaces, and provides a simple flag/option API. Arguments are stored as `StringView` slices into the original string — no heap allocation until a `Vector<StringView>` is grown internally.

**Usage:**
```cpp
CommandLine cmd("kernel debug log_level=3 no_acpi");
if (cmd.HasFlag("debug"))            EnableDebugMode();
if (cmd.HasFlag("no_acpi"))          DisableAcpi();
auto level = cmd.GetOption("log_level").ValueOr("1");
```
