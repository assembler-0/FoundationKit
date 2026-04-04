#include <Test/TestFramework.hpp>

extern "C" {
    int main() {
        return FoundationKitCxxStl::Test::TestRegistry::RunAll();
    }
}
