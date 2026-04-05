# FoundationKitCxxAbi — Part 2: Itanium Demangler

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxAbi::Demangle` | **Header dir:** `FoundationKitCxxAbi/Include/FoundationKitCxxAbi/`

---

## 2.1 The Kernel Crash Trace Problem

When standard POSIX applications crash, stack unwinding facilities routinely utilize `abi::__cxa_demangle` to translate raw Itanium-mangled C++ symbols strings (e.g. `_ZN3foo3barEv`) back into human-readable contexts (`foo::bar()`). 

Standard library implementations of this demangler universally depend on heavy dynamic heap allocation (`malloc()` / `free()`) to construct the abstract syntax tree during evaluation. 

This presents a fatal paradox for a bare-metal kernel: **If the kernel crashes due to memory heap corruption, attempting to demangle symbols using `malloc()` during the crash handler will immediately trigger a recursive fault, completely destroying the diagnostic trace.**

### The FoundationKit Zero-Heap Strategy
`FoundationKitCxxAbi::Demangle` establishes an entirely custom, subset-specific Itanium demangler executing pure **zero-allocation recursive-descent string scanning**. It constructs the translated string directly into a user-supplied, statically bounded buffer. No abstract syntax trees are built, and absolutely no dynamic bindings are invoked.

---

## 2.2 Core `Demangle` API Usage

Instead of passing pointers that dynamically scale, users pass an explicit pre-allocated `Span<char>` or raw array.

```cpp
#include <FoundationKitCxxAbi/Demangle/Demangler.hpp>

// 1. Create a bounded stack buffer safely inside the crash handler
char buffer[1024];

// 2. Pass mangled trace from ELF section
int status = 0;
usize written = FoundationKitCxxAbi::Demangle::Demangle(
    "_ZN17FoundationKitCore9MemoryMgr11AllocateRawEmm", 
    buffer, 
    sizeof(buffer), 
    &status
);

// If status == 0, buffer == "FoundationKitCore::MemoryMgr::AllocateRaw(unsigned long, unsigned long)"
```

### ABI Standard Fallback Header
If integrated into standard POSIX toolchains relying blindly on standard linkage, it exposes the conforming interface:
```cpp
extern "C" char* __cxa_demangle(const char* mangled_name, 
                                char* buf, 
                                size_t* n, 
                                int* status);
```
**Constraint:** If `buf` is null and POSIX requests us to allocate on our own via `malloc`, FoundationKit forcibly returns `status = -1` (OOM), rigidly forbidding any heap-based backdoor calls.

---

## 2.3 Recursive-Descent Engine (`DemangleAbi.cpp`)

The parser processes Itanium grammar without lookaheads:

### State Management (`DemangleState`)
A lightweight, stack-hosted context aggregates pointers, bounds checks, and limits parser depth to prevent stack overflow from infinitely recursive symbols:

```cpp
struct DemangleState {
    StringScanner input;
    StringSink    output;
    u16           depth;  // Hard recursion firewall

    bool HasError() const;
};
```

### Demangling Syntax Subsets
Supported Grammar Coverage:
1. **Types:** Primitive Encodings (`i` -> `int`, `m` -> `unsigned long`, `v` -> `void`, `b` -> `bool`).
2. **Pointers/References:** `P`, `R`, `O`.
3. **Names:** Direct sizes (`3foo` -> `foo`), Nested Encapsulations (`N...E` -> `foo::bar`).
4. **Constructors / Destructors:** `C1`, `C2` / `D1`, `D2`.
5. **Basic Operators:** `nw`, `dl` (`new`, `delete`), arithmetic combinations.

### Output Rendering (`StringSink`)
As the parser cascades forward, it feeds direct text characters into a bounds-checked `StringSink`. If the mangled symbol proves overly complex—or requests string space exceeding the provided buffer—the sink truncates gracefully without invoking undefined layout memory bounds violations, ensuring the host OS successfully receives partial stack trace signatures.
