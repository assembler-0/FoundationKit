#include <Test/TestFramework.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitCxxStl::Test {

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
            
            // Log test start
            ::FoundationKitOsl::OslLog("| running test: ");
            ::FoundationKitOsl::OslLog(curr->Name);
            ::FoundationKitOsl::OslLog("\n");

            curr->Func();

            if (s_current_test_failed) {
                failed++;
                ::FoundationKitOsl::OslLog("|----> FAILED\n");
            } else {
                ::FoundationKitOsl::OslLog("|----> PASSED\n");
            }
        }

        return failed;
    }

    void TestRegistry::ReportFailure(const char* expr, const char* file, usize line) noexcept {
        (void)line;
        ::FoundationKitOsl::OslLog("| Assertion failed: ");
        ::FoundationKitOsl::OslLog(expr);
        ::FoundationKitOsl::OslLog(" at ");
        ::FoundationKitOsl::OslLog(file);
        ::FoundationKitOsl::OslLog("\n");
    }

} // namespace FoundationKitCxxStl::Test
