#include <Test/TestFramework.hpp>
#include <print>

namespace FoundationKit::Test {

    TestCase* TestRegistry::s_first = nullptr;
    bool TestRegistry::s_current_test_failed = false;

    void TestRegistry::Register(TestCase* test_case) noexcept {
        test_case->Next = s_first;
        s_first = test_case;
    }

    i32 TestRegistry::RunAll() noexcept {
        i32 passed = 0;
        i32 failed = 0;
        i32 total = 0;

        // Count tests (linked list is in reverse order of registration)
        for (TestCase* curr = s_first; curr != nullptr; curr = curr->Next) {
            total++;
        }

        std::println("[==========] Running {} tests.", total);

        // We run them in the order they are in the list (reverse registration)
        for (TestCase* curr = s_first; curr != nullptr; curr = curr->Next) {
            std::println("[ RUN      ] {}", curr->Name);
            s_current_test_failed = false;
            
            curr->Func();

            if (s_current_test_failed) {
                std::println("[  FAILED  ] {}", curr->Name);
                failed++;
            } else {
                std::println("[       OK ] {}", curr->Name);
                passed++;
            }
        }

        std::println("[==========] {} tests ran.", total);
        std::println("[  PASSED  ] {} tests.", passed);
        if (failed > 0) {
            std::println("[  FAILED  ] {} tests.", failed);
        }

        return failed;
    }

    void TestRegistry::ReportFailure(const char* expression, const char* file, usize line) noexcept {
        std::println("{}:{}: Failure: assertion '{}' failed.", file, line, expression);
    }

} // namespace FoundationKit::Test
