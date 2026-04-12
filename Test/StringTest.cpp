#include <FoundationKitCxxStl/Base/String.hpp>
#include <FoundationKitCxxStl/Base/StringUtil.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;

TEST_CASE(StringView_Basic) {
    StringView sv = "Hello, World!";
    ASSERT_EQ(sv.Size(), 13);
    ASSERT_EQ(sv[0], 'H');
    ASSERT_EQ(sv.At(7), 'W');
    ASSERT_EQ(sv.Front(), 'H');
    ASSERT_EQ(sv.Back(), '!');
}

TEST_CASE(StringView_Find) {
    StringView sv = "The quick brown fox jumps over the lazy dog";
    ASSERT_EQ(sv.Find('q'), 4);
    ASSERT_EQ(sv.Find("fox"), 16);
    ASSERT_EQ(sv.Find('d', 40), 40);
    ASSERT_EQ(sv.Find("cat"), StringView::NPos);
}

TEST_CASE(StringView_RFind) {
    StringView sv = "Hello, Hello, Hello!";
    ASSERT_EQ(sv.RFind('H'), 14);
    ASSERT_EQ(sv.RFind('H', 10), 7);
    ASSERT_EQ(sv.RFind('e', 5), 1);
    ASSERT_EQ(sv.RFind('x'), StringView::NPos);
}

TEST_CASE(StringView_FirstLastOf) {
    StringView sv = "Hello, World!";
    ASSERT_EQ(sv.FindFirstOf("aeiou"), 1); // 'e'
    ASSERT_EQ(sv.FindLastOf("aeiou"), 8);  // 'o' in World
    ASSERT_EQ(sv.FindFirstNotOf("Hello"), 5); // ','
    ASSERT_EQ(sv.FindLastNotOf("!"), 11); // 'd'
}

TEST_CASE(StringView_Trim) {
    StringView sv = "  \t  Hello, World!  \n  ";
    StringView trimmed = sv.Trim();
    ASSERT_EQ(trimmed, "Hello, World!");

    StringView sv2 = "xxxHello, World!yyy";
    ASSERT_EQ(sv2.Trim("xy"), "Hello, World!");
}

TEST_CASE(StringView_PrefixSuffix) {
    StringView sv = "Hello, World!";
    sv.RemovePrefix(7);
    ASSERT_EQ(sv, "World!");
    sv.RemoveSuffix(1);
    ASSERT_EQ(sv, "World");
}

TEST_CASE(String_Trim) {
    String<> s;
    s.Append("  Hello  ").Value();
    s.Trim();
    ASSERT_EQ(static_cast<StringView>(s), "Hello");
    ASSERT_EQ(s.Size(), 5);
}

TEST_CASE(StringUtil_CStrOps) {
    char buf[32];
    StringUtil::StrCpy(buf, "Hello");
    ASSERT_EQ(StringUtil::StrLen(buf), 5);
    ASSERT_EQ(StringUtil::StrCmp(buf, "Hello"), 0);
    ASSERT_EQ(StringUtil::StrCmp(buf, "Hella"), 14);
    
    ASSERT_EQ(StringUtil::StrCaseCmp("HELLO", "hello"), 0);
    
    const char* found = StringUtil::StrStr("Hello World", "World");
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(StringUtil::StrCmp(found, "World"), 0);
}
