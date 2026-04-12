#include <Test/TestFramework.hpp>
#include <FoundationKitCxxStl/Base/RefCounted.hpp>
#include <FoundationKitCxxStl/Base/RefPtr.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>

using namespace FoundationKitCxxStl;

namespace {
    static int g_destroy_count = 0;

    class TestObject : public RefCounted<TestObject> {
    public:
        TestObject() {
            // RefCountedBase initializes with ref count = 1
        }
        
        void Destroy() const noexcept {
            g_destroy_count++;
            // Memory is typically reclaimed here for heap allocations,
            // but we use stack allocation for these tests so we do nothing.
        }
    };
}

TEST_CASE(RefCountedBase_Basic) {
    RefCountedBase ref;
    ASSERT_EQ(ref.GetRefCount(), 1u);
    
    ref.AddRef();
    ASSERT_EQ(ref.GetRefCount(), 2u);
    
    bool reached_zero = ref.ReleaseRef();
    ASSERT_FALSE(reached_zero);
    ASSERT_EQ(ref.GetRefCount(), 1u);
    
    reached_zero = ref.ReleaseRef();
    ASSERT_TRUE(reached_zero);
}

TEST_CASE(RefPtr_Ownership) {
    g_destroy_count = 0;
    
    TestObject obj;
    ASSERT_EQ(obj.GetRefCount(), 1u);
    
    {
        // Constructor taking a raw pointer takes ownership of the +1 initial ref count.
        RefPtr<TestObject> ptr(&obj);
        ASSERT_EQ(ptr->GetRefCount(), 1u);
        ASSERT_EQ(ptr.Get(), &obj);
        
        {
            RefPtr<TestObject> ptr2 = ptr;
            ASSERT_EQ(obj.GetRefCount(), 2u);
            ASSERT_EQ(ptr2.Get(), &obj);
        } // ptr2 destroyed, ref count drops back to 1
        
        ASSERT_EQ(obj.GetRefCount(), 1u);
    } // ptr destroyed, ref count drops to 0, matching the Destroy() call
    
    ASSERT_EQ(g_destroy_count, 1);
}

TEST_CASE(RefPtr_MoveAndSwap) {
    g_destroy_count = 0;
    
    TestObject obj1;
    TestObject obj2;
    
    RefPtr<TestObject> ptr1(&obj1);
    RefPtr<TestObject> ptr2(&obj2);
    
    ASSERT_EQ(ptr1.Get(), &obj1);
    ASSERT_EQ(ptr2.Get(), &obj2);
    
    ptr1.Swap(ptr2);
    
    ASSERT_EQ(ptr1.Get(), &obj2);
    ASSERT_EQ(ptr2.Get(), &obj1);
    
    RefPtr<TestObject> ptr3 = Move(ptr1);
    ASSERT_EQ(ptr3.Get(), &obj2);
    ASSERT_EQ(ptr1.Get(), nullptr);
    
    // Reset ptr3 manually
    ptr3.Reset();
    ASSERT_EQ(ptr3.Get(), nullptr);
    
    // obj2 was bound to ptr3 and destructed when reset
    ASSERT_EQ(g_destroy_count, 1);
    
    // ptr2 has obj1, goes out of scope here
    ptr2.Reset();
    ASSERT_EQ(g_destroy_count, 2);
}

TEST_CASE(RefPtr_Comparison) {
    TestObject obj1;
    TestObject obj2;
    
    RefPtr<TestObject> ptr1(&obj1);
    RefPtr<TestObject> ptr2(&obj2);
    RefPtr<TestObject> ptr1_copy(ptr1);
    RefPtr<TestObject> null_ptr;

    ASSERT_EQ(ptr1, ptr1_copy);
    ASSERT_NE(ptr1, ptr2);
    ASSERT_NE(ptr1, null_ptr);
    ASSERT_EQ(null_ptr, nullptr);
    ASSERT_FALSE(null_ptr);
    ASSERT_TRUE(ptr1);
}
