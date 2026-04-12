#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/StringBuilder.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;

TEST_CASE(Format_Basic) {
    StringBuilder sb;
    sb.Format("Hello {}!", "World");
    EXPECT_EQ(sb.View(), "Hello World!");
}

TEST_CASE(Format_Integer) {
    StringBuilder sb;
    sb.Format("{:d} {:x} {:X} {:b} {:o}", 42, 42, 42, 42, 42);
    EXPECT_EQ(sb.View(), "42 2a 2A 101010 52");
}

TEST_CASE(Format_Padding) {
    StringBuilder sb1;
    sb1.Format("{:5}", 42);
    EXPECT_EQ(sb1.View(), "   42");

    StringBuilder sb2;
    sb2.Format("{:<5}", 42);
    EXPECT_EQ(sb2.View(), "42   ");

    StringBuilder sb3;
    sb3.Format("{:^5}", 42);
    EXPECT_EQ(sb3.View(), " 42  ");
}

TEST_CASE(Format_Fill) {
    StringBuilder sb;
    sb.Format("{:*^5}", 42);
    EXPECT_EQ(sb.View(), "*42**");
}

TEST_CASE(Format_Sign) {
    StringBuilder sb;
    sb.Format("{:+d} {: d} {:+d}", 42, 42, -42);
    EXPECT_EQ(sb.View(), "+42  42 -42");
}

TEST_CASE(Format_Alternate) {
    StringBuilder sb;
    sb.Format("{:#x} {:#b} {:#o}", 42, 42, 42);
    EXPECT_EQ(sb.View(), "0x2a 0b101010 052");
}

TEST_CASE(Format_ZeroPad) {
    StringBuilder sb;
    sb.Format("{:05d}", 42);
    EXPECT_EQ(sb.View(), "00042");
}

TEST_CASE(Format_Float) {
    StringBuilder sb;
    sb.Format("{:.2f}", 3.14159);
    EXPECT_EQ(sb.View(), "3.14");
}

TEST_CASE(Format_Escaping) {
    StringBuilder sb;
    sb.Format("{{Hello}} {} {{}}", "World");
    EXPECT_EQ(sb.View(), "{Hello} World {}");
}
