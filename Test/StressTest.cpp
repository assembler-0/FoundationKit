#include <Test/TestFramework.hpp>

// Base components
#include <FoundationKit/Base/Array.hpp>
#include <FoundationKit/Base/Vector.hpp>
#include <FoundationKit/Base/String.hpp>
#include <FoundationKit/Base/StringView.hpp>
#include <FoundationKit/Base/Optional.hpp>
#include <FoundationKit/Base/Expected.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/NumericLimits.hpp>

// Memory components
#include <FoundationKit/Memory/BumpAllocator.hpp>
#include <FoundationKit/Memory/AnyAllocator.hpp>
#include <FoundationKit/Memory/UniquePtr.hpp>
#include <FoundationKit/Memory/SharedPtr.hpp>

using namespace FoundationKit;
using namespace FoundationKit::Memory;

// Global test allocator
static byte g_test_buffer[128 * 1024]; // 128KB should be enough
static BumpAllocator g_test_alloc(g_test_buffer, sizeof(g_test_buffer));
static ConcreteResource g_test_resource(g_test_alloc);

TEST_CASE(Base_FixedArray) {
    FixedArray arr = {10, 20, 30, 40, 50};
    ASSERT_EQ(arr.Size(), 5);
    ASSERT_EQ(arr[0], 10);
    ASSERT_EQ(arr[4], 50);
    
    auto val = arr.At(2);
    ASSERT_TRUE(val.HasValue());
    ASSERT_EQ(*val, 30);
    
    arr.Fill(100);
    for (usize i = 0; i < arr.Size(); ++i) {
        ASSERT_EQ(arr[i], 100);
    }
}

TEST_CASE(Base_Vector) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc(&g_test_resource);
    
    Vector<i32> vec(any_alloc);
    for (i32 i = 0; i < 50; ++i) {
        auto res = vec.PushBack(i * 2);
        ASSERT_TRUE(res.HasValue());
    }
    
    ASSERT_EQ(vec.Size(), 50);
    ASSERT_EQ(vec[0], 0);
    ASSERT_EQ(vec[25], 50);
    ASSERT_EQ(vec[49], 98);
    
    vec.PopBack();
    ASSERT_EQ(vec.Size(), 49);
    ASSERT_EQ(vec.Back(), 96);
}

TEST_CASE(Base_String) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc(&g_test_resource);
    
    String str(any_alloc);
    auto res1 = str.Append("Foundation");
    ASSERT_TRUE(res1.HasValue());
    auto res2 = str.Append("Kit");
    ASSERT_TRUE(res2.HasValue());
    
    ASSERT_EQ(str.Size(), 13);
    ASSERT_EQ(StringCompare(StringView(str), "FoundationKit"), 0);
    
    // Test SSO to Heap transition
    String long_str(any_alloc);
    auto res3 = long_str.Append("This is a very long string that should definitely exceed the SSO capacity of 23 characters.");
    ASSERT_TRUE(res3.HasValue());
    ASSERT_TRUE(long_str.Size() > 23);
    ASSERT_EQ(long_str.CStr()[long_str.Size()], '\0');
}

TEST_CASE(Base_Optional_Expected) {
    Optional<i32> opt;
    ASSERT_FALSE(opt.HasValue());
    opt = 42;
    ASSERT_TRUE(opt.HasValue());
    ASSERT_EQ(*opt, 42);

    // Reference test
    i32 val_ref = 100;
    const Optional<i32&> opt_ref(val_ref);
    ASSERT_TRUE(opt_ref.HasValue());
    ASSERT_EQ(*opt_ref, 100);
    val_ref = 200;
    ASSERT_EQ(*opt_ref, 200);
    
    auto func = [](const bool fail) -> Expected<i32, const char*> {
        if (fail) return "Error";
        return 100;
    };
    
    auto res1 = func(false);
    ASSERT_TRUE(res1.HasValue());
    ASSERT_EQ(*res1, 100);
    
    auto res2 = func(true);
    ASSERT_FALSE(res2.HasValue());
    ASSERT_EQ(StringCompare(StringView(res2.Error()), "Error"), 0);
}

TEST_CASE(Memory_SmartPointers) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc(&g_test_resource);
    
    static i32 dtor_count = 0;
    struct Guard {
        Guard() { dtor_count = 0; }
        ~Guard() { dtor_count++; }
    };
    
    {
        auto uptr = MakeUnique<Guard>(any_alloc);
        ASSERT_TRUE(static_cast<bool>(uptr));
    }
    ASSERT_EQ(dtor_count, 1);
    
    dtor_count = 0;
    {
        auto res = TryAllocateShared<Guard>(any_alloc);
        ASSERT_TRUE(res.HasValue());
        auto sptr1 = Move(res.Value());
        {
            auto sptr2 = sptr1;
            ASSERT_EQ(sptr1.UseCount(), 2);
        }
        ASSERT_EQ(sptr1.UseCount(), 1);
    }
    ASSERT_EQ(dtor_count, 1);
}

TEST_CASE(Base_NumericLimits) {
    ASSERT_TRUE(NumericLimits<u32>::IsSpecialized);
    ASSERT_EQ(NumericLimits<u32>::Min(), 0);
    ASSERT_EQ(NumericLimits<u32>::Max(), 0xFFFFFFFFU);
    
    ASSERT_TRUE(NumericLimits<i32>::IsSigned);
    ASSERT_EQ(NumericLimits<i32>::Max(), 2147483647);
    ASSERT_EQ(NumericLimits<i32>::Min(), -2147483647 - 1);
}

TEST_CASE(Meta_Concepts) {
    ASSERT_TRUE(Integral<i32>);
    ASSERT_TRUE(FloatingPoint<f32>);
    ASSERT_FALSE(Integral<f32>);
    ASSERT_TRUE(Pointer<i32*>);
    ASSERT_TRUE((SameAs<i32, i32>));
}

TEST_CASE(Memory_Allocators) {
    g_test_alloc.DeallocateAll();

    auto res = g_test_alloc.Allocate(100, 8);
    ASSERT_TRUE(res.ok());
    ASSERT_EQ(res.size, 100);
    ASSERT_TRUE(g_test_alloc.Owns(res.ptr));
    
    g_test_alloc.DeallocateAll();
    ASSERT_EQ(g_test_alloc.Remaining(), sizeof(g_test_buffer));
}
