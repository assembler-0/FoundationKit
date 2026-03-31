#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitCxxStl::Test {

    /// @brief A function pointer type for test functions.
    typedef void (*TestFunction)();

    /// @brief Represents a single test case in the registry.
    struct TestCase {
        const char*  Name;
        TestFunction Func;
        TestCase*    Next;
    };

    /// @brief Manages the registration and execution of test cases.
    class TestRegistry {
    public:
        /// @brief Register a new test case.
        static void Register(TestCase* test_case) noexcept;

        /// @brief Run all registered test cases.
        /// @return The number of failed tests.
        static i32 RunAll() noexcept;

        /// @brief Report an assertion failure.
        static void ReportFailure(const char* expression, const char* file, usize line) noexcept;

        /// @brief Signal that the current test has failed.
        static void SetCurrentTestFailed() noexcept { s_current_test_failed = true; }

    private:
        static TestCase* s_first;
        static bool      s_current_test_failed;
    };

    /// @brief Helper struct for static registration of test cases.
    struct TestRegistrar {
        explicit TestRegistrar(TestCase* test_case) noexcept {
            TestRegistry::Register(test_case);
        }
    };

} // namespace FoundationKitCxxStl::Test

/// @section Test Macros

#define TEST_CASE(name) \
    static void test_func_##name(); \
    static FoundationKitCxxStl::Test::TestCase test_case_##name = { #name, test_func_##name, nullptr }; \
    static FoundationKitCxxStl::Test::TestRegistrar test_registrar_##name(&test_case_##name); \
    static void test_func_##name()

#define ASSERT_TRUE(expr) \
    if (!(expr)) { \
        FoundationKitCxxStl::Test::TestRegistry::ReportFailure(#expr, __FILE__, __LINE__); \
        FoundationKitCxxStl::Test::TestRegistry::SetCurrentTestFailed(); \
        return; \
    }

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(val1, val2) ASSERT_TRUE((val1) == (val2))
#define ASSERT_NE(val1, val2) ASSERT_TRUE((val1) != (val2))

#define EXPECT_TRUE(expr) \
    if (!(expr)) { \
        FoundationKitCxxStl::Test::TestRegistry::ReportFailure(#expr, __FILE__, __LINE__); \
        FoundationKitCxxStl::Test::TestRegistry::SetCurrentTestFailed(); \
    }

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ(val1, val2) EXPECT_TRUE((val1) == (val2))
#define EXPECT_NE(val1, val2) EXPECT_TRUE((val1) != (val2))
