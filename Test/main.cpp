#include <Test/TestFramework.hpp>
#include <FoundationKit/Osl/Osl.hpp>
#include <ostream>
#include <print>

void fk_panic(const char* msg) {
    std::print("{}", msg);
    exit(1);
}

int main() {
    return FoundationKit::Test::TestRegistry::RunAll();
}
