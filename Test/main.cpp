#include <Test/TestFramework.hpp>
#include <FoundationKit/Osl/Osl.hpp>
#include <ostream>
#include <print>

extern "C" {
    void FoundationKitOslBug(const char* msg) {
        std::print("{}", msg);
        exit(1);
    }
    bool FoundationKitOslIsCpuFeaturesEnabled() { return true; }
    void FoundationKitOslLog(const char* msg) {
        std::print("{}", msg);
    }
}

int main() {
    return FoundationKit::Test::TestRegistry::RunAll();
}
