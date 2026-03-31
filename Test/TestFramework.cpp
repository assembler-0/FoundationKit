#include <Test/TestFramework.hpp>

namespace FoundationKit::Test {

    TestCase* TestRegistry::s_first = nullptr;
    bool TestRegistry::s_current_test_failed = false;

    void TestRegistry::Register(TestCase* test_case) noexcept {
        test_case->Next = s_first;
        s_first = test_case;
    }

    i32 TestRegistry::RunAll() noexcept {
        i32 failed = 0;

        for (TestCase* curr = s_first; curr != nullptr; curr = curr->Next) {
            s_current_test_failed = false;
            
            curr->Func();

            if (s_current_test_failed) {
                failed++;
            }
        }

        return failed;
    }

    void TestRegistry::ReportFailure(const char*, const char*, usize) noexcept {}

} // namespace FoundationKit::Test
