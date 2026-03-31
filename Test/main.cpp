#include <Test/TestFramework.hpp>
#include <FoundationKitCxxStl/Osl/Osl.hpp>

extern "C" {
    [[noreturn]] void FoundationKitCxxStlOslBug(const char*) {
        int dummy = 0;
        while (true) {
            FoundationKitCxxStl::Base::CompilerBuiltins::DoNotOptimize(dummy);
        }
    }

    bool FoundationKitCxxStlOslIsCpuFeaturesEnabled() {
        return true; 
    }

    void FoundationKitCxxStlOslLog(const char*) {}

    int main() {
        return FoundationKitCxxStl::Test::TestRegistry::RunAll();
    }
}
