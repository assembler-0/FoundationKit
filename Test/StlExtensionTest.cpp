#include <FoundationKitCxxStl/Base/Bit.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/FixedVector.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Tuple.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;

TEST_CASE(Bit_Utilities) {
    ASSERT_EQ(Byteswap(static_cast<u16>(0x1234)), 0x3412);
    ASSERT_EQ(Byteswap(static_cast<u32>(0x12345678)), 0x78563412);
    
    ASSERT_TRUE(HasSingleBit(1u));
    ASSERT_TRUE(HasSingleBit(2u));
    ASSERT_TRUE(HasSingleBit(4u));
    ASSERT_FALSE(HasSingleBit(0u));
    ASSERT_FALSE(HasSingleBit(3u));
    
    ASSERT_EQ(BitCeil(0u), 1u);
    ASSERT_EQ(BitCeil(1u), 1u);
    ASSERT_EQ(BitCeil(2u), 2u);
    ASSERT_EQ(BitCeil(3u), 4u);
    ASSERT_EQ(BitCeil(4u), 4u);
    ASSERT_EQ(BitCeil(5u), 8u);
    
    ASSERT_EQ(BitFloor(0u), 0u);
    ASSERT_EQ(BitFloor(1u), 1u);
    ASSERT_EQ(BitFloor(2u), 2u);
    ASSERT_EQ(BitFloor(3u), 2u);
    ASSERT_EQ(BitFloor(4u), 4u);
    ASSERT_EQ(BitFloor(7u), 4u);
    
    ASSERT_EQ(BitWidth(0u), 0);
    ASSERT_EQ(BitWidth(1u), 1);
    ASSERT_EQ(BitWidth(2u), 2);
    ASSERT_EQ(BitWidth(3u), 2);
    ASSERT_EQ(BitWidth(4u), 3);
}

TEST_CASE(Expected_Refined) {
    Expected<int, const char*> e1(42);
    ASSERT_TRUE(e1.HasValue());
    ASSERT_EQ(*e1, 42);
    
    Expected<int, const char*> e2 = Unexpected("error");
    ASSERT_FALSE(e2.HasValue());
    ASSERT_EQ(StringView(e2.Error()), "error");
    
    Expected<void, int> e3;
    ASSERT_TRUE(e3.HasValue());
    
    Expected<void, int> e4 = Unexpected(5);
    ASSERT_FALSE(e4.HasValue());
    ASSERT_EQ(e4.Error(), 5);

    Expected<int, int> e5(InPlace, 10);
    ASSERT_TRUE(e5.HasValue());
    ASSERT_EQ(*e5, 10);
}

TEST_CASE(Tuple_Basic) {
    Tuple<int, char, bool> t(1, 'a', true);
    ASSERT_EQ(Get<0>(t), 1);
    ASSERT_EQ(Get<1>(t), 'a');
    ASSERT_EQ(Get<2>(t), true);
    
    Get<0>(t) = 42;
    ASSERT_EQ(Get<0>(t), 42);
    
    auto t2 = MakeTuple(10, 3.14f);
    ASSERT_EQ(Get<0>(t2), 10);
}

TEST_CASE(StaticVector_Basic) {
    FixedVector<int, 5> v;
    ASSERT_TRUE(v.Empty());
    ASSERT_EQ(v.Size(), 0);
    
    ASSERT_TRUE(v.PushBack(1));
    ASSERT_TRUE(v.PushBack(2));
    ASSERT_TRUE(v.PushBack(3));
    ASSERT_EQ(v.Size(), 3);
    ASSERT_FALSE(v.Empty());
    
    ASSERT_EQ(v[0], 1);
    ASSERT_EQ(v[1], 2);
    ASSERT_EQ(v[2], 3);
    
    v.PopBack();
    ASSERT_EQ(v.Size(), 2);
    ASSERT_EQ(v.Back(), 2);
    
    v.Clear();
    ASSERT_TRUE(v.Empty());
    ASSERT_EQ(v.Size(), 0);
    
    FixedVector<int, 2> v2;
    ASSERT_TRUE(v2.PushBack(10));
    ASSERT_TRUE(v2.PushBack(20));
    ASSERT_FALSE(v2.PushBack(30)); // Full
    ASSERT_TRUE(v2.Full());
}
