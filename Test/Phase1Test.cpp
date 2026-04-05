#include <Test/TestFramework.hpp>
#include <FoundationKitCxxStl/Base/KernelError.hpp>
#include <FoundationKitCxxStl/Base/FixedString.hpp>
#include <FoundationKitCxxStl/Structure/RingBuffer.hpp>
#include <FoundationKitCxxStl/Structure/InterruptSafeQueue.hpp>
#include <FoundationKitPlatform/Bitfield.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;
using namespace FoundationKitPlatform;

// =============================================================================
// KernelError / KernelResult
// =============================================================================

TEST_CASE(KernelError_ResultValue) {
    KernelResult<int> ok = 42;
    EXPECT_TRUE(ok.HasValue());
    EXPECT_EQ(ok.Value(), 42);
}

TEST_CASE(KernelError_ResultError) {
    KernelResult<int> err = Unexpected(KernelError::OutOfMemory);
    EXPECT_FALSE(err.HasValue());
    EXPECT_EQ(err.Error(), KernelError::OutOfMemory);
}

TEST_CASE(KernelError_VoidResult) {
    KernelResult<void> ok;
    EXPECT_TRUE(ok.HasValue());

    KernelResult<void> err = Unexpected(KernelError::PermissionDenied);
    EXPECT_FALSE(err.HasValue());
    EXPECT_EQ(err.Error(), KernelError::PermissionDenied);
}

TEST_CASE(KernelError_AllCodes) {
    // Ensure every enumerator is distinct and round-trips through KernelResult.
    auto check = [](KernelError e) {
        KernelResult<int> r = Unexpected(e);
        return r.Error() == e;
    };
    EXPECT_TRUE(check(KernelError::OutOfMemory));
    EXPECT_TRUE(check(KernelError::InvalidArgument));
    EXPECT_TRUE(check(KernelError::NotFound));
    EXPECT_TRUE(check(KernelError::PermissionDenied));
    EXPECT_TRUE(check(KernelError::Timeout));
    EXPECT_TRUE(check(KernelError::DeviceBusy));
    EXPECT_TRUE(check(KernelError::NotSupported));
    EXPECT_TRUE(check(KernelError::Overflow));
}

// =============================================================================
// FixedString<N>
// =============================================================================

TEST_CASE(FixedString_DefaultEmpty) {
    FixedString<16> s;
    EXPECT_TRUE(s.Empty());
    EXPECT_EQ(s.Size(), 0u);
    EXPECT_EQ(s.CStr()[0], '\0');
}

TEST_CASE(FixedString_ConstructFromLiteral) {
    FixedString<16> s("hello");
    EXPECT_EQ(s.Size(), 5u);
    EXPECT_EQ(s[0], 'h');
    EXPECT_EQ(s[4], 'o');
    EXPECT_EQ(s.Front(), 'h');
    EXPECT_EQ(s.Back(), 'o');
}

TEST_CASE(FixedString_AppendAndView) {
    FixedString<32> s("foo");
    s.Append(StringView("bar"));
    EXPECT_EQ(s.Size(), 6u);
    EXPECT_EQ(static_cast<StringView>(s), StringView("foobar"));
}

TEST_CASE(FixedString_AppendChar) {
    FixedString<8> s;
    s.Append('A');
    s.Append('B');
    s.Append('C');
    EXPECT_EQ(s.Size(), 3u);
    EXPECT_EQ(s[2], 'C');
}

TEST_CASE(FixedString_Clear) {
    FixedString<16> s("kernel");
    s.Clear();
    EXPECT_TRUE(s.Empty());
    EXPECT_EQ(s.CStr()[0], '\0');
}

TEST_CASE(FixedString_Equality) {
    FixedString<16> a("dev0");
    FixedString<16> b("dev0");
    FixedString<16> c("dev1");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a == StringView("dev0"));
}

TEST_CASE(FixedString_StartsWith_EndsWith_Contains) {
    FixedString<32> s("uart0");
    EXPECT_TRUE(s.StartsWith("uart"));
    EXPECT_FALSE(s.StartsWith("eth"));
    EXPECT_TRUE(s.EndsWith("0"));
    EXPECT_FALSE(s.EndsWith("1"));
    EXPECT_TRUE(s.Contains("art"));
    EXPECT_FALSE(s.Contains("xyz"));
}

TEST_CASE(FixedString_ExactCapacity) {
    // Fill to exactly N characters — must not crash.
    FixedString<4> s;
    s.Append("abcd");
    EXPECT_EQ(s.Size(), 4u);
    EXPECT_EQ(s.CStr()[4], '\0');
}

// =============================================================================
// StaticRingBuffer<T, N> — SPSC
// =============================================================================

TEST_CASE(StaticRingBuffer_PushPop) {
    StaticRingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.Empty());
    EXPECT_TRUE(rb.Push(1));
    EXPECT_TRUE(rb.Push(2));
    EXPECT_TRUE(rb.Push(3));
    EXPECT_EQ(rb.Size(), 3u);

    auto v = rb.Pop();
    ASSERT_TRUE(v.HasValue());
    EXPECT_EQ(v.Value(), 1);

    v = rb.Pop();
    ASSERT_TRUE(v.HasValue());
    EXPECT_EQ(v.Value(), 2);

    EXPECT_EQ(rb.Size(), 1u);
}

TEST_CASE(StaticRingBuffer_Full) {
    // Capacity is N-1 = 3 usable slots for N=4.
    StaticRingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.Push(10));
    EXPECT_TRUE(rb.Push(20));
    EXPECT_TRUE(rb.Push(30));
    EXPECT_FALSE(rb.Push(40)); // full
    EXPECT_EQ(rb.Size(), 3u);
}

TEST_CASE(StaticRingBuffer_Empty) {
    StaticRingBuffer<int, 4> rb;
    EXPECT_FALSE(rb.Pop().HasValue());
}

TEST_CASE(StaticRingBuffer_WrapAround) {
    // Push 3, pop 3, push 3 again — exercises the index wrap.
    StaticRingBuffer<int, 4> rb;
    (void)rb.Push(1); (void)rb.Push(2); (void)rb.Push(3);
    (void)rb.Pop();   (void)rb.Pop();   (void)rb.Pop();
    EXPECT_TRUE(rb.Empty());

    EXPECT_TRUE(rb.Push(4));
    EXPECT_TRUE(rb.Push(5));
    EXPECT_TRUE(rb.Push(6));

    EXPECT_EQ(rb.Pop().Value(), 4);
    EXPECT_EQ(rb.Pop().Value(), 5);
    EXPECT_EQ(rb.Pop().Value(), 6);
    EXPECT_TRUE(rb.Empty());
}

TEST_CASE(StaticRingBuffer_FifoOrder) {
    StaticRingBuffer<int, 16> rb;
    for (int i = 0; i < 10; ++i) (void)rb.Push(i);
    for (int i = 0; i < 10; ++i) {
        auto v = rb.Pop();
        ASSERT_TRUE(v.HasValue());
        EXPECT_EQ(v.Value(), i);
    }
}

TEST_CASE(StaticRingBuffer_Capacity) {
    constexpr usize cap8  = StaticRingBuffer<int, 8>::Capacity();
    constexpr usize cap16 = StaticRingBuffer<int, 16>::Capacity();
    EXPECT_EQ(cap8,  7u);
    EXPECT_EQ(cap16, 15u);
}

// =============================================================================
// InterruptSafeQueue<T, N>
// =============================================================================

TEST_CASE(InterruptSafeQueue_PushFromIsrPopFromThread) {
    InterruptSafeQueue<u32, 8> q;
    EXPECT_TRUE(q.Empty());

    EXPECT_TRUE(q.PushFromIsr(100u));
    EXPECT_TRUE(q.PushFromIsr(200u));
    EXPECT_EQ(q.Size(), 2u);

    auto v = q.PopFromThread();
    ASSERT_TRUE(v.HasValue());
    EXPECT_EQ(v.Value(), 100u);

    v = q.PopFromThread();
    ASSERT_TRUE(v.HasValue());
    EXPECT_EQ(v.Value(), 200u);

    EXPECT_TRUE(q.Empty());
}

TEST_CASE(InterruptSafeQueue_FullRejectsPush) {
    InterruptSafeQueue<int, 4> q; // 3 usable slots
    EXPECT_TRUE(q.PushFromIsr(1));
    EXPECT_TRUE(q.PushFromIsr(2));
    EXPECT_TRUE(q.PushFromIsr(3));
    EXPECT_FALSE(q.PushFromIsr(4));
}

TEST_CASE(InterruptSafeQueue_EmptyReturnsEmpty) {
    InterruptSafeQueue<int, 4> q;
    EXPECT_FALSE(q.PopFromThread().HasValue());
}

TEST_CASE(InterruptSafeQueue_FifoOrder) {
    InterruptSafeQueue<int, 16> q;
    for (int i = 0; i < 8; ++i) (void)q.PushFromIsr(i);
    for (int i = 0; i < 8; ++i) {
        auto v = q.PopFromThread();
        ASSERT_TRUE(v.HasValue());
        EXPECT_EQ(v.Value(), i);
    }
}

// =============================================================================
// Bitfield<T, Offset, Width>
// =============================================================================

TEST_CASE(Bitfield_ExtractSingleBit) {
    using F = Bitfield<u32, 3, 1>;
    EXPECT_EQ(F::Extract(0x08u), 1u);
    EXPECT_EQ(F::Extract(0x00u), 0u);
    EXPECT_EQ(F::Extract(0xFFFFFFFFu), 1u);
}

TEST_CASE(Bitfield_ExtractMultiBit) {
    // Bits [7:4] of u8
    using F = Bitfield<u8, 4, 4>;
    EXPECT_EQ(F::Extract(0xABu), 0x0Au);
    EXPECT_EQ(F::Extract(0x00u), 0x00u);
    EXPECT_EQ(F::Extract(0xFFu), 0x0Fu);
}

TEST_CASE(Bitfield_Insert) {
    using F = Bitfield<u32, 4, 4>;
    // Insert 0xC into bits [7:4] of 0x000000AB
    EXPECT_EQ(F::Insert(0xABu, 0xCu), 0xCBu);
    // Insert 0 into 0xFF clears bits [7:4], leaving bits [3:0] intact → 0x0F
    EXPECT_EQ(F::Insert(0xFFu, 0x0u), 0x0Fu);
    // Insert 0 into a full u32 clears only the field
    EXPECT_EQ(F::Insert(0xFFFFFFFFu, 0x0u), 0xFFFFFF0Fu);
}

TEST_CASE(Bitfield_InsertDoesNotCorruptAdjacentBits) {
    using F = Bitfield<u32, 8, 8>;
    const u32 reg = 0xDEADBEEFu;
    const u32 result = F::Insert(reg, 0xAAu);
    // Bits [7:0] and [31:16] must be unchanged
    EXPECT_EQ(result & 0xFFFF00FFu, reg & 0xFFFF00FFu);
    EXPECT_EQ(F::Extract(result), 0xAAu);
}

TEST_CASE(Bitfield_IsSetIsClear) {
    using F = Bitfield<u32, 5, 1>;
    EXPECT_TRUE(F::IsSet(0x20u));
    EXPECT_FALSE(F::IsSet(0x00u));
    EXPECT_TRUE(F::IsClear(0x00u));
    EXPECT_FALSE(F::IsClear(0x20u));
}

TEST_CASE(Bitfield_SetClear) {
    using F = Bitfield<u32, 2, 1>;
    EXPECT_EQ(F::Set(0x00u), 0x04u);
    EXPECT_EQ(F::Clear(0xFFu), 0xFBu);
}

TEST_CASE(Bitfield_Mask) {
    EXPECT_EQ((Bitfield<u32, 0, 8>::Mask), 0x000000FFu);
    EXPECT_EQ((Bitfield<u32, 8, 8>::Mask), 0x0000FF00u);
    EXPECT_EQ((Bitfield<u64, 31, 1>::Mask), 0x80000000ull);
}

TEST_CASE(Bitfield_FullWidthU32) {
    using F = Bitfield<u32, 0, 32>;
    EXPECT_EQ(F::Extract(0xDEADBEEFu), 0xDEADBEEFu);
    EXPECT_EQ(F::Insert(0u, 0xCAFEBABEu), 0xCAFEBABEu);
}
