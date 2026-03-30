#include <FoundationKit/Base/Array.hpp>
#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Memory/PlacementNew.hpp>

using namespace FoundationKit;

extern "C" {
    void FoundationKitOslBug(const char*) {
        while (true) { __asm__ volatile("hlt"); }
    }
    bool FoundationKitOslIsCpuFeaturesEnabled() { return true; }
    void FoundationKitOslLog(const char*) {}
}

struct TestObject {
    int x;
    int y;
    static int dtor_called;
    TestObject(int a, int b) : x(a), y(b) {}
    ~TestObject() { dtor_called++; }
};
int TestObject::dtor_called = 0;

extern "C" int main() {
    FixedArray<int, 3> arr = {1, 2, 3};
    if (arr.Size() != 3) return 1;
    if (arr[0] != 1 || arr[1] != 2 || arr[2] != 3) return 2;

    // 2. Test Placement New
    alignas(TestObject) byte buffer[sizeof(TestObject)];
    TestObject* obj = new (buffer) TestObject(42, 1337);
    
    if (obj->x != 42 || obj->y != 1337) return 3;
    
    obj->~TestObject();
    if (TestObject::dtor_called != 1) return 4;

    return 0; // Success
}
