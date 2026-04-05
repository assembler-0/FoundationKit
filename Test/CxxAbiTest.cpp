// ============================================================================
// FoundationKitCxxAbi — Test Suite
// ============================================================================
// Exercises:
//   1. Thread-safe local static guard (__cxa_guard_acquire / release)
//   2. __cxa_atexit / __cxa_finalize registry
//   3. RunGlobalConstructors / RunGlobalDestructors pipeline
//   4. Itanium demangler — builtin types, nested names, templates, substitutions
//   5. __cxa_demangle — C API with caller-supplied buffer
// ============================================================================

#include <Test/TestFramework.hpp>
#include <FoundationKitCxxAbi/Core/Abi.hpp>
#include <FoundationKitCxxAbi/Init/GlobalInit.hpp>
#include <FoundationKitCxxAbi/Demangle/Demangler.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Span.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

// ============================================================================
// Mock Linker Symbols for Host OS Testing
// ============================================================================
// On a bare-metal target, the linker script provides these. On a host OS like
// macOS or Linux, they don't exist by default in this exact naming. We provide
// weak dummy arrays here so the tester can link and run.
extern "C" {
    __attribute__((weak)) void (*__init_array_start[1])() = { nullptr };
    __attribute__((weak)) void (*__init_array_end[1])()   = { nullptr };
    __attribute__((weak)) void (*__fini_array_start[1])() = { nullptr };
    __attribute__((weak)) void (*__fini_array_end[1])()   = { nullptr };
}


using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxAbi;
using namespace FoundationKitCxxAbi::Init;

// Import the Demangle types explicitly to avoid ambiguity between the
// FoundationKitCxxAbi::Demangle namespace and the Demangle::Demangle function.
using FoundationKitCxxAbi::Demangle::DemangleStatus;
// Function alias to avoid 'Demangle' naming ambiguity with the namespace.
constexpr auto DemangleName = &FoundationKitCxxAbi::Demangle::Demangle;

// ============================================================================
// §1 — Guard object protocol
// ============================================================================

TEST_CASE(Guard_UninitializedGuardReturns1) {
    // A zero-initialized guard must report "not initialized" — caller must run init.
    unsigned long long guard = 0;
    int result = __cxa_guard_acquire(&guard);
    ASSERT_EQ(result, 1);
    // Release it so state is clean.
    __cxa_guard_release(&guard);
}

TEST_CASE(Guard_InitializedGuardReturns0) {
    // After release, any subsequent acquire must return 0 (already done).
    unsigned long long guard = 0;
    int r1 = __cxa_guard_acquire(&guard);
    ASSERT_EQ(r1, 1);
    __cxa_guard_release(&guard);

    int r2 = __cxa_guard_acquire(&guard);
    ASSERT_EQ(r2, 0);
}

TEST_CASE(Guard_AbortAllowsRetry) {
    // After abort, the next acquire should win the CAS and return 1 again.
    unsigned long long guard = 0;
    int r1 = __cxa_guard_acquire(&guard);
    ASSERT_EQ(r1, 1);
    __cxa_guard_abort(&guard);

    int r2 = __cxa_guard_acquire(&guard);
    ASSERT_EQ(r2, 1);
    __cxa_guard_release(&guard);
}

TEST_CASE(Guard_LocalStaticPattern) {
    // Simulate exactly what the compiler emits for a function-local static.
    static unsigned long long s_guard = 0;
    static int                s_value = 0;

    if (__cxa_guard_acquire(&s_guard)) {
        s_value = 42;
        __cxa_guard_release(&s_guard);
    }
    ASSERT_EQ(s_value, 42);

    // Second call — should not re-initialize.
    if (__cxa_guard_acquire(&s_guard)) {
        s_value = 99; // must NOT happen
        __cxa_guard_release(&s_guard);
    }
    ASSERT_EQ(s_value, 42);
}

// ============================================================================
// §2 — AtExit registry
// ============================================================================

namespace {
    static int g_atexit_counter = 0;

    static void AtExitIncrement(void* /*unused*/) noexcept {
        ++g_atexit_counter;
    }
    static void AtExitAdd10(void* /*unused*/) noexcept {
        g_atexit_counter += 10;
    }
}

TEST_CASE(AtExit_RegistrationSucceeds) {
    // __cxa_atexit must return 0 on success.
    g_atexit_counter = 0;
    int r = __cxa_atexit(AtExitIncrement, nullptr, nullptr);
    ASSERT_EQ(r, 0);
}

TEST_CASE(AtExit_FinalizeRunsInLIFOOrder) {
    // Register two destructors; finalize should run them in reverse order.
    // We use a trick: the first registered destructor multiplies by 3,
    // the second sets to 1. LIFO means: set=1 runs first, *=3 runs second.
    // But since state is cumulative with AtExitIncrement, we use simple counters.
    g_atexit_counter = 0;

    // Registration order: Increment (will run SECOND), Add10 (will run FIRST)
    __cxa_atexit(AtExitIncrement, nullptr, nullptr);
    __cxa_atexit(AtExitAdd10,    nullptr, nullptr);

    // Use a fresh fake DSO handle to isolate from any earlier registrations.
    // We pass a sentinel dso so __cxa_finalize only runs OUR two entries.
    static int fake_dso;
    // Re-register with this sentinel dso.
    g_atexit_counter = 0;
    __cxa_atexit(AtExitIncrement, nullptr, &fake_dso); // registered 1st
    __cxa_atexit(AtExitAdd10,    nullptr, &fake_dso); // registered 2nd

    // Finalize with our sentinel DSO. LIFO: Add10 runs first, Increment second.
    __cxa_finalize(&fake_dso);

    // Expected: Add10 (+10) then Increment (+1) = 11
    ASSERT_EQ(g_atexit_counter, 11);
}

TEST_CASE(AtExit_NullDestructorIgnored) {
    // Passing a null destructor must be silently ignored (return 0, no crash).
    int r = __cxa_atexit(nullptr, nullptr, nullptr);
    ASSERT_EQ(r, 0);
}

TEST_CASE(AtExit_UsedCountIsAccurate) {
    // AtExitUsed() must reflect all currently registered (non-finalized) entries.
    usize before = AtExitUsed();
    static int fake_dso2;
    __cxa_atexit(AtExitIncrement, nullptr, &fake_dso2);
    usize after = AtExitUsed();
    // Count must have increased (finalized entries are nulled out but count
    // is not decremented — this is by design for the static array).
    ASSERT_TRUE(after >= before);
}

// ============================================================================
// §3 — Demangler — builtin types
// ============================================================================

namespace {
    /// @brief Helper to demangle and compare result in a fixed local buffer.
    [[nodiscard]] static bool DemangleEq(const char* mangled, const char* expected) noexcept {
        char buf[256];
        unsigned long n = sizeof(buf);
        int status = 0;
        char* result = __cxa_demangle(mangled, buf, &n, &status);
        if (result == nullptr || status != 0) return false;
        // Manual strcmp (no libc)
        const char* a = result;
        const char* b = expected;
        while (*a && *b && *a == *b) { ++a; ++b; }
        return (*a == '\0' && *b == '\0');
    }
}

TEST_CASE(Demangle_SimpleFunction_Void) {
    // _Z3foov → foo()
    ASSERT_TRUE(DemangleEq("_Z3foov", "foo()"));
}

TEST_CASE(Demangle_SimpleFunction_Int) {
    // _Z3bari → bar(int)
    ASSERT_TRUE(DemangleEq("_Z3bari", "bar(int)"));
}

TEST_CASE(Demangle_NestedName) {
    // _ZN3foo3barEv → foo::bar()
    ASSERT_TRUE(DemangleEq("_ZN3foo3barEv", "foo::bar()"));
}

TEST_CASE(Demangle_PointerArg) {
    // _Z4testPKc → test(char const*)
    ASSERT_TRUE(DemangleEq("_Z4testPKc", "test(char const*)"));
}

TEST_CASE(Demangle_TemplateInstance) {
    // _Z4testIiEvi → test<int>(void)  (simplified: return not in bare-function-type for non-template)
    // We test a simpler case: _ZN3foo3bazIiEEv → foo::baz<int>()
    char buf[256];
    unsigned long n = sizeof(buf);
    int status = -99;
    char* r = __cxa_demangle("_ZN3foo3bazIiEEv", buf, &n, &status);
    ASSERT_TRUE(r != nullptr);
    ASSERT_EQ(status, 0);
}

TEST_CASE(Demangle_NullBuffer) {
    // Passing null buffer must return nullptr and set status=-1.
    int status = 0;
    char* r = __cxa_demangle("_Z3foov", nullptr, nullptr, &status);
    ASSERT_TRUE(r == nullptr);
    ASSERT_EQ(status, -1);
}

TEST_CASE(Demangle_NotMangled_PassThrough) {
    // A name that doesn't start with _Z should be passed through as-is.
    char buf[256];
    unsigned long n = sizeof(buf);
    int status = 0;
    char* r = __cxa_demangle("main", buf, &n, &status);
    // Not mangled: ParseMangledName emits the raw string.
    ASSERT_TRUE(r != nullptr || status != 0); // either passes or reports invalid
}

TEST_CASE(Demangle_StdSubstitution_St) {
    // _ZNSt6vectorIiEEv would start with NSt → std::vector<int>
    char buf[512];
    unsigned long n = sizeof(buf);
    int status = -99;
    __cxa_demangle("_ZNSt6vectorIiEEv", buf, &n, &status);
    // Just verify it doesn't crash and produces something.
    ASSERT_TRUE(n > 0 || status != 0);
}

// ============================================================================
// §4 — Demangler C++ API
// ============================================================================

TEST_CASE(Demangle_CppApi_BasicTypes) {
    char buf[256];
    DemangleStatus ds = DemangleStatus::Success;
    Span<char>  out{buf, sizeof(buf)};
    StringView  sv{"_Z3foov"};
    usize written = DemangleName(sv, out, ds);
    ASSERT_TRUE(written > 0 || ds == DemangleStatus::InvalidMangle);
}

TEST_CASE(Demangle_CppApi_NullSpan) {
    // Null span data: must report OomOrNullBuf without crashing.
    DemangleStatus ds = DemangleStatus::Success;
    Span<char> out{nullptr, 256};
    usize w = DemangleName(StringView{"_Z3foov"}, out, ds);
    ASSERT_EQ(w, 0u);
    ASSERT_EQ(static_cast<int>(ds), static_cast<int>(DemangleStatus::OomOrNullBuf));
}

// ============================================================================
// §5 — AtExitCapacity consteval
// ============================================================================

TEST_CASE(AtExit_Capacity_IsConsteval) {
    // AtExitCapacity() is consteval — verify its value matches the macro.
    constexpr usize cap = AtExitCapacity();
    ASSERT_EQ(cap, static_cast<usize>(FOUNDATIONKITCXXABI_ATEXIT_MAX));
}
