#include <FoundationKitCxxStl/Base/CharType.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;

TEST_CASE(CharType_IsDigit) {
    ASSERT_TRUE(CharType::IsDigit('0'));
    ASSERT_TRUE(CharType::IsDigit('5'));
    ASSERT_TRUE(CharType::IsDigit('9'));
    ASSERT_FALSE(CharType::IsDigit('a'));
    ASSERT_FALSE(CharType::IsDigit(' '));
}

TEST_CASE(CharType_IsAlpha) {
    ASSERT_TRUE(CharType::IsAlpha('a'));
    ASSERT_TRUE(CharType::IsAlpha('Z'));
    ASSERT_FALSE(CharType::IsAlpha('0'));
    ASSERT_FALSE(CharType::IsAlpha('$'));
}

TEST_CASE(CharType_IsAlnum) {
    ASSERT_TRUE(CharType::IsAlnum('a'));
    ASSERT_TRUE(CharType::IsAlnum('0'));
    ASSERT_FALSE(CharType::IsAlnum(' '));
}

TEST_CASE(CharType_IsSpace) {
    ASSERT_TRUE(CharType::IsSpace(' '));
    ASSERT_TRUE(CharType::IsSpace('\t'));
    ASSERT_TRUE(CharType::IsSpace('\n'));
    ASSERT_FALSE(CharType::IsSpace('a'));
}

TEST_CASE(CharType_IsUpperLower) {
    ASSERT_TRUE(CharType::IsUpper('A'));
    ASSERT_FALSE(CharType::IsUpper('a'));
    ASSERT_TRUE(CharType::IsLower('z'));
    ASSERT_FALSE(CharType::IsLower('Z'));
}

TEST_CASE(CharType_IsXDigit) {
    ASSERT_TRUE(CharType::IsXDigit('0'));
    ASSERT_TRUE(CharType::IsXDigit('a'));
    ASSERT_TRUE(CharType::IsXDigit('F'));
    ASSERT_FALSE(CharType::IsXDigit('g'));
}

TEST_CASE(CharType_IsPunct) {
    ASSERT_TRUE(CharType::IsPunct('!'));
    ASSERT_TRUE(CharType::IsPunct('.'));
    ASSERT_FALSE(CharType::IsPunct('a'));
    ASSERT_FALSE(CharType::IsPunct(' '));
}

TEST_CASE(CharType_IsPrintGraph) {
    ASSERT_TRUE(CharType::IsPrint(' '));
    ASSERT_TRUE(CharType::IsPrint('a'));
    ASSERT_FALSE(CharType::IsPrint('\t'));
    
    ASSERT_TRUE(CharType::IsGraph('a'));
    ASSERT_FALSE(CharType::IsGraph(' '));
}

TEST_CASE(CharType_Convert) {
    ASSERT_EQ(CharType::ToLower('A'), 'a');
    ASSERT_EQ(CharType::ToLower('a'), 'a');
    ASSERT_EQ(CharType::ToUpper('a'), 'A');
    ASSERT_EQ(CharType::ToUpper('A'), 'A');
    ASSERT_EQ(CharType::ToLower('1'), '1');
}
