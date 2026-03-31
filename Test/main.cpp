#include <Test/TestFramework.hpp>
#include <FoundationKit/Osl/Osl.hpp>

extern "C" {
    [[noreturn]] void FoundationKitOslBug(const char*) {
        int dummy = 0;
        while (true) {
            FoundationKit::Base::CompilerBuiltins::DoNotOptimize(dummy);
        }
    }

    bool FoundationKitOslIsCpuFeaturesEnabled() {
        return true; 
    }

    void FoundationKitOslLog(const char*) {}

    int main() {
        return FoundationKit::Test::TestRegistry::RunAll();
    }
}
