#include <Test/TestFramework.hpp>

// Base components
#include <FoundationKitCxxStl/Base/Array.hpp>
#include <FoundationKitCxxStl/Base/Vector.hpp>
#include <FoundationKitCxxStl/Base/String.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/StringBuilder.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/NumericLimits.hpp>
#include <FoundationKitCxxStl/Base/Pair.hpp>
#include <FoundationKitCxxStl/Base/Span.hpp>
#include <FoundationKitCxxStl/Base/Variant.hpp>
#include <FoundationKitCxxStl/Base/Bit.hpp>
#include <FoundationKitCxxStl/Base/Algorithm.hpp>
#include <FoundationKitCxxStl/Base/CommandLine.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>

// Structure components
#include <FoundationKitCxxStl/Structure/SinglyLinkedList.hpp>
#include <FoundationKitCxxStl/Structure/DoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Structure/CircularLinkedList.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Structure/BitSet.hpp>
#include <FoundationKitCxxStl/Structure/HashMap.hpp>

// Memory components
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/SlabAllocator.hpp>
#include <FoundationKitMemory/BuddyAllocator.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>
#include <FoundationKitMemory/UniquePtr.hpp>
#include <FoundationKitMemory/SharedPtr.hpp>
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/GlobalAllocator.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;
using namespace FoundationKitCxxStl::Structure;

// Global test allocator
static byte g_test_buffer[128 * 1024]; // 128KB should be enough
static BumpAllocator g_test_alloc(g_test_buffer, sizeof(g_test_buffer));

// Forward declare for Variant test
struct Complex {
    static i32 dtor_called;
    i32 val;
    Complex(i32 v) : val(v) {}
    ~Complex() { dtor_called++; }
};
i32 Complex::dtor_called = 0;

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
    AnyAllocator any_alloc{};
    
    Vector<i32> vec(any_alloc);
    for (i32 i = 0; i < 50; ++i) {
        auto res = vec.PushBack(i * 2);
        ASSERT_TRUE(res);
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
    AnyAllocator any_alloc{};
    
    String str(any_alloc);
    auto res1 = str.Append("Foundation");
    ASSERT_TRUE(res1.HasValue());
    auto res2 = str.Append("KitCxxStl");
    ASSERT_TRUE(res2.HasValue());
    
    ASSERT_EQ(str.Size(), 19);
    ASSERT_EQ(StringCompare(StringView(str), "FoundationKitCxxStl"), 0);
    
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
        if (fail) return Expected<i32, const char *>("Error");
        return Expected<i32, const char *>(100);
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
    AnyAllocator any_alloc{};
    
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
        auto res = static_cast<Expected<SharedPtr<Guard>, MemoryError>>(TryAllocateShared<Guard>(any_alloc));
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

TEST_CASE(Base_StringBuilder) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    StringBuilder sb(any_alloc);
    sb.Append("Answer: ").Append(42).Append(", Hex: ").Append("deadbeef");
    auto str = Move(sb).Build();
    ASSERT_EQ(StringCompare(StringView(str), "Answer: 42, Hex: deadbeef"), 0);

    StringBuilder sb2(any_alloc);
    sb2.Format("User: {}, ID: {}, Score: {}", "Assembler", 1337, 99);
    auto str2 = Move(sb2).Build();
    ASSERT_EQ(StringCompare(StringView(str2), "User: Assembler, ID: 1337, Score: 99"), 0);

    // Test with signed/unsigned/negative
    StringBuilder sb3(any_alloc);
    sb3.Format("Values: {}, {}, {}", -123, 0, 456789);
    ASSERT_EQ(StringCompare(sb3.View(), "Values: -123, 0, 456789"), 0);
}

TEST_CASE(Base_StringOps) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};

    String str(any_alloc);
    str.Append("  Hello FoundationKitCxxStl  ");
    
    ASSERT_TRUE(str.Contains("Foundation"));
    ASSERT_TRUE(str.StartsWith("  Hello"));
    ASSERT_TRUE(str.EndsWith("Stl  "));
    
    str.Trim();
    ASSERT_EQ(StringCompare(StringView(str), "Hello FoundationKitCxxStl"), 0);
    ASSERT_EQ(str.Size(), 25);

    auto res_sub = str.SubStr(6, 10);
    ASSERT_TRUE(res_sub.HasValue());
    ASSERT_EQ(StringCompare(StringView(res_sub.Value()), "Foundation"), 0);

    ASSERT_EQ(str.Find("Kit"), 16);
    ASSERT_EQ(str.Find("Missing"), static_cast<usize>(-1));
}

TEST_CASE(Base_PairSpan) {
    Pair<i32, const char*> p1(1, "One");
    ASSERT_EQ(p1.first, 1);
    ASSERT_EQ(StringCompare(p1.second, "One"), 0);

    auto p2 = MakePair(10.5f, true);
    ASSERT_EQ(p2.first, 10.5f);
    ASSERT_TRUE(p2.second);

    i32 raw_arr[] = {1, 2, 3, 4, 5};
    Span<i32> span(raw_arr);
    ASSERT_EQ(span.Size(), 5);
    ASSERT_EQ(span[2], 3);
    
    auto sub = span.SubSpan(1, 3);
    ASSERT_EQ(sub.Size(), 3);
    ASSERT_EQ(sub[0], 2);
    ASSERT_EQ(sub[2], 4);
}

TEST_CASE(Base_Variant) {
    Variant<i32, f32, const char*> v;
    ASSERT_FALSE(v.IsValid());

    v = 42;
    ASSERT_TRUE(v.Is<i32>());
    ASSERT_EQ(*v.GetIf<i32>(), 42);

    v = 3.14f;
    ASSERT_TRUE(v.Is<f32>());
    ASSERT_EQ(*v.GetIf<f32>(), 3.14f);

    v = "Hello";
    ASSERT_TRUE(v.Is<const char*>());
    ASSERT_EQ(StringCompare(*v.GetIf<const char*>(), "Hello"), 0);

    Complex::dtor_called = 0;
    {
        Variant<i32, Complex> v2 = Complex(10);
        ASSERT_TRUE(v2.Is<Complex>());
        v2 = 20; // Should trigger Complex dtor
    }
    ASSERT_EQ(Complex::dtor_called, 2); // 1 for temp in assignment, 1 for Reset when assigning 20
}

TEST_CASE(Memory_PoolAllocator) {
    byte pool_buffer[1024];
    PoolAllocator<64, 8> pool;
    pool.Initialize(pool_buffer, sizeof(pool_buffer));

    auto res1 = pool.Allocate(64, 8);
    ASSERT_TRUE(res1.ok());
    ASSERT_TRUE(pool.Owns(res1.ptr));

    auto res2 = pool.Allocate(64, 8);
    ASSERT_TRUE(res2.ok());
    ASSERT_NE(res1.ptr, res2.ptr);

    pool.Deallocate(res1.ptr, 64);
    auto res3 = pool.Allocate(64, 8);
    ASSERT_EQ(res3.ptr, res1.ptr); // Should reuse

    // Exhaust pool
    for (int i = 0; i < 14; ++i) { // 1024 / 64 = 16. Already have 2 (one reused).
        (void)pool.Allocate(64, 8);
    }
    auto res_fail = pool.Allocate(64, 8);
    ASSERT_FALSE(res_fail.ok());
}

TEST_CASE(Structure_SinglyLinkedList) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    SinglyLinkedList<i32> list(any_alloc);
    for (i32 i = 0; i < 10; ++i) {
        ASSERT_TRUE(list.PushFront(i));
    }
    ASSERT_EQ(list.Size(), 10);
    ASSERT_EQ(list.Front(), 9);
    
    i32 val = 9;
    for (auto& item : list) {
        ASSERT_EQ(item, val--);
    }
    
    list.PopFront();
    ASSERT_EQ(list.Size(), 9);
    ASSERT_EQ(list.Front(), 8);
}

TEST_CASE(Structure_DoublyLinkedList) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    DoublyLinkedList<i32> list(any_alloc);
    for (i32 i = 0; i < 5; ++i) {
        ASSERT_TRUE(list.PushBack(i));
    }
    ASSERT_EQ(list.Size(), 5);
    ASSERT_EQ(list.Front(), 0);
    ASSERT_EQ(list.Back(), 4);
    
    list.PopBack();
    ASSERT_EQ(list.Size(), 4);
    ASSERT_EQ(list.Back(), 3);
}

TEST_CASE(Structure_CircularLinkedList) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    CircularLinkedList<i32> list(any_alloc);
    list.PushBack(1);
    list.PushBack(2);
    list.PushBack(3);
    
    ASSERT_EQ(list.Size(), 3);
    ASSERT_EQ(list.Front(), 1);
    ASSERT_EQ(list.Back(), 3);
    
    list.Rotate();
    ASSERT_EQ(list.Front(), 2);
    ASSERT_EQ(list.Back(), 1);
}

TEST_CASE(Structure_IntrusiveDoublyLinkedList) {
    struct Item {
        IntrusiveDoublyLinkedListNode link;
        i32 value;
        Item(i32 v) : value(v) {}
    };
    
    FoundationKitCxxStl::Structure::IntrusiveDoublyLinkedList list;
    Item i1(1), i2(2), i3(3);
    
    list.PushBack(&i1.link);
    list.PushBack(&i2.link);
    list.PushBack(&i3.link);
    
    ASSERT_EQ(list.Size(), 3);
    
    auto* node = list.Begin();
    auto* item = ContainerOf<Item, &Item::link>(node);
    ASSERT_EQ(item->value, 1);
    
    list.Remove(&i2.link);
    ASSERT_EQ(list.Size(), 2);
    
    node = node->next;
    item = ContainerOf<Item, &Item::link>(node);
    ASSERT_EQ(item->value, 3);
}

TEST_CASE(Base_BitManipulation) {
    u32 val = 0b00000000000000000000000000001010; // 10
    ASSERT_EQ(PopCount(val), 2);
    ASSERT_EQ(CountTrailingZeros(val), 1);
    ASSERT_EQ(CountLeadingZeros(val), 28);
    
    u32 rot = 0x80000001U;
    ASSERT_EQ(RotateLeft(rot, 1), 0x00000003U);
    ASSERT_EQ(RotateRight(0x00000003U, 1), rot);
    
    ASSERT_TRUE(IsPowerOfTwo(1024U));
    ASSERT_FALSE(IsPowerOfTwo(1023U));
}

TEST_CASE(Base_Algorithm) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    Vector<i32> vec(any_alloc);
    vec.PushBack(5);
    vec.PushBack(2);
    vec.PushBack(9);
    vec.PushBack(1);
    vec.PushBack(5);
    vec.PushBack(6);
    
    Sort(vec.begin(), vec.end());
    
    ASSERT_EQ(vec[0], 1);
    ASSERT_EQ(vec[1], 2);
    ASSERT_EQ(vec[2], 5);
    ASSERT_EQ(vec[3], 5);
    ASSERT_EQ(vec[4], 6);
    ASSERT_EQ(vec[5], 9);
    
    ASSERT_TRUE(BinarySearch(vec.begin(), vec.end(), 5));
    ASSERT_FALSE(BinarySearch(vec.begin(), vec.end(), 10));
}

TEST_CASE(Structure_BitSet) {
    BitSet<100> bs;
    ASSERT_TRUE(bs.None());
    
    bs.Set(10);
    bs.Set(50);
    bs.Set(99);
    
    ASSERT_TRUE(bs.Test(10));
    ASSERT_TRUE(bs.Test(50));
    ASSERT_TRUE(bs.Test(99));
    ASSERT_FALSE(bs.Test(0));
    ASSERT_EQ(bs.Count(), 3);
    
    ASSERT_EQ(bs.FindFirstSet(), 10);
    bs.Reset(10);
    ASSERT_EQ(bs.FindFirstSet(), 50);
    
    bs.Reset();
    ASSERT_TRUE(bs.None());
    ASSERT_EQ(bs.FindFirstUnset(), 0);
}

TEST_CASE(Base_Algorithm_Manipulation) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    Vector<i32> vec(any_alloc);
    vec.PushBack(1);
    vec.PushBack(2);
    vec.PushBack(3);
    vec.PushBack(4);
    
    Reverse(vec.begin(), vec.end());
    ASSERT_EQ(vec[0], 4);
    ASSERT_EQ(vec[3], 1);
    
    Rotate(vec.begin(), vec.begin() + 1, vec.end());
    // [4,3,2,1] rotate by 1 -> [3,2,1,4]
    ASSERT_EQ(vec[0], 3);
    ASSERT_EQ(vec[3], 4);
    
    vec.Clear();
    vec.PushBack(1);
    vec.PushBack(1);
    vec.PushBack(2);
    vec.PushBack(2);
    vec.PushBack(3);
    
    auto it = Unique(vec.begin(), vec.end());
    ASSERT_EQ(it - vec.begin(), 3);
    ASSERT_EQ(vec[0], 1);
    ASSERT_EQ(vec[1], 2);
    ASSERT_EQ(vec[2], 3);
    
    vec.Clear();
    for (i32 i = 1; i <= 10; ++i) vec.PushBack(i);
    auto it2 = RemoveIf(vec.begin(), vec.end(), [](i32 x) { return x % 2 == 0; });
    ASSERT_EQ(it2 - vec.begin(), 5);
    ASSERT_EQ(vec[0], 1);
    ASSERT_EQ(vec[1], 3);
}

TEST_CASE(Base_MinMax_InitializerList) {
    ASSERT_EQ(Min({5, 2, 9, 1, 7}), 1);
    ASSERT_EQ(Max({5, 2, 9, 1, 7}), 9);
    ASSERT_EQ(Clamp(10, 1, 5), 5);
}

TEST_CASE(Structure_HashMap) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};
    
    HashMap<i32, const char*> map(any_alloc);
    ASSERT_TRUE(map.Insert(1, "One"));
    ASSERT_TRUE(map.Insert(2, "Two"));
    ASSERT_TRUE(map.Insert(100, "Hundred"));
    
    auto val1 = map.Get(1);
    ASSERT_TRUE(val1.HasValue());
    ASSERT_EQ(StringCompare(*val1, "One"), 0);
    
    auto val2 = map.Get(5);
    ASSERT_FALSE(val2.HasValue());
    
    ASSERT_TRUE(map.Remove(2));
    ASSERT_FALSE(map.Get(2).HasValue());
    ASSERT_EQ(map.Size(), 2);
    
    // Test Rehash
    for (i32 i = 1000; i < 1050; ++i) {
        ASSERT_TRUE(map.Insert(i, "Value"));
    }
    ASSERT_TRUE(map.Size() > 50);
}

TEST_CASE(Base_Extended_Suite) {
    g_test_alloc.DeallocateAll();
    // Global allocator is initialized in main.cpp
    AnyAllocator any_alloc{};

    // 1. CommandLine Parsing
    CommandLine cmd("kernel.bin debug log_level=3 --force root=/dev/sda1");
    ASSERT_TRUE(cmd.HasFlag("debug"));
    ASSERT_TRUE(cmd.HasFlag("--force"));
    ASSERT_FALSE(cmd.HasFlag("missing"));
    
    auto level = cmd.GetOption("log_level");
    ASSERT_TRUE(level.HasValue());
    ASSERT_EQ(StringCompare(*level, "3"), 0);
    
    auto root = cmd.GetOption("root");
    ASSERT_TRUE(root.HasValue());
    ASSERT_EQ(StringCompare(*root, "/dev/sda1"), 0);
    
    ASSERT_EQ(cmd.ArgumentCount(), 5);
    ASSERT_EQ(StringCompare(*cmd.GetArgument(0), "kernel.bin"), 0);

    // 2. String::Split
    String csv(any_alloc);
    csv.Append("one,two,three,four");
    auto parts_res = csv.Split(',');
    ASSERT_TRUE(parts_res.HasValue());
    auto& parts = *parts_res;
    ASSERT_EQ(parts.Size(), 4);
    ASSERT_EQ(StringCompare(StringView(parts[0]), "one"), 0);
    ASSERT_EQ(StringCompare(StringView(parts[1]), "two"), 0);
    ASSERT_EQ(StringCompare(StringView(parts[2]), "three"), 0);
    ASSERT_EQ(StringCompare(StringView(parts[3]), "four"), 0);

    // 3. Extended Algorithms
    Vector<i32> vec(any_alloc);
    for (i32 i = 1; i <= 5; ++i) vec.PushBack(i);

    // AnyOf/AllOf/NoneOf
    ASSERT_TRUE(AnyOf(vec.begin(), vec.end(), [](i32 x) { return x == 3; }));
    ASSERT_TRUE(AllOf(vec.begin(), vec.end(), [](i32 x) { return x > 0; }));
    ASSERT_TRUE(NoneOf(vec.begin(), vec.end(), [](i32 x) { return x > 10; }));

    // Find/FindIf
    auto it_find = Find(vec.begin(), vec.end(), 4);
    ASSERT_NE(it_find, vec.end());
    ASSERT_EQ(*it_find, 4);

    auto it_find_if = FindIf(vec.begin(), vec.end(), [](i32 x) { return x % 2 == 0 && x > 2; });
    ASSERT_NE(it_find_if, vec.end());
    ASSERT_EQ(*it_find_if, 4);

    // Accumulate
    i32 sum = Accumulate(vec.begin(), vec.end(), 0);
    ASSERT_EQ(sum, 15);

    // Transform
    Vector<i32> squared(any_alloc);
    squared.Resize(vec.Size());
    Transform(vec.begin(), vec.end(), squared.begin(), [](i32 x) { return x * x; });
    ASSERT_EQ(squared[0], 1);
    ASSERT_EQ(squared[4], 25);

    // Count/CountIf
    vec.PushBack(3);
    ASSERT_EQ(Count(vec.begin(), vec.end(), 3), 2);
    ASSERT_EQ(CountIf(vec.begin(), vec.end(), [](i32 x) { return x % 2 != 0; }), 4); // 1, 3, 5, 3

    // 4. Formatting Extensions
    StringBuilder sb(any_alloc);
    sb.Format("Base10: {}, Base16: {}, Char: {}, String: {}", 255, "FF", 'A', StringView("Test"));
    ASSERT_EQ(StringCompare(sb.View(), "Base10: 255, Base16: FF, Char: A, String: Test"), 0);
}

TEST_CASE(Base_LoggingAndFormatting) {
    g_test_alloc.DeallocateAll();
    AnyAllocator any_alloc{};

    // Test Log (non-formatted)
    Log(LogLevel::Info, "Testing non-formatted Info log");
    Log(LogLevel::Warning, "Testing non-formatted Warning log");

    // Test LogFmt
    LogFmt(LogLevel::Info, "Testing formatted log: {}, {}", "Hello", 12345);

    // Test Macros
    FK_LOG_INFO("Macro Info: {}", 1);
    FK_LOG_WARN("Macro Warning: {}", 2);
    FK_LOG_ERR("Macro Error: {}", 3);

    // Test StaticStringBuilder directly
    StaticStringBuilder<128> ssb;
    ssb.Format("Static: {}, {}, {}", 100, "String", 'Z');
    ASSERT_EQ(StringCompare(StringView(ssb.CStr()), "Static: 100, String, Z"), 0);

    // Test multiple arguments and different types in Format
    StringBuilder sb(any_alloc);
    sb.Format("Mixed: {} {} {} {} {}", -1, 2u, 'A', "BCD", StringView("EFG"));
    ASSERT_EQ(StringCompare(sb.View(), "Mixed: -1 2 A BCD EFG"), 0);
}
