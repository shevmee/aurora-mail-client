#include <gtest/gtest.h>

#include <string>

#include "Parser.hpp"
#include "ResponseType.hpp"

using namespace aurora::mail::smtp::response;

TEST(SmtpParserTest, ParseSimpleResponse)
{
  auto result = parse("250 OK\r\n");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, 250);
  EXPECT_EQ(result->text, "OK");
}

TEST(SmtpParserTest, ParseWithEnhancedCode)
{
  auto result = parse("250 2.0.0 OK Message accepted\r\n");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, 250);
  ASSERT_TRUE(result->enhanced_code.has_value());
  EXPECT_TRUE(result->enhanced_code->isSuccess());
  EXPECT_EQ(result->text, "OK Message accepted");
}

TEST(SmtpParserTest, ParseMultiLine)
{
  auto result = parse("250-First line\r\n250-Second line\r\n250 Last line\r\n");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, 250);
  EXPECT_TRUE(result->text.find("First line") != std::string::npos);
  EXPECT_TRUE(result->text.find("Last line") != std::string::npos);
}

TEST(SmtpParserTest, TrimTrailingWhitespace)
{
  auto result = parse("250 OK\r\n");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, 250);
  EXPECT_EQ(result->text, "OK");
}

TEST(SmtpParserTest, RtrimRemovesTrailingWhitespaceAndPreservesInnerAndLeadingCharacters)
{
  const auto str = std::string{ "  abc \r\n\t " };
  const auto expected = std::string{ "  abc" };

  std::size_t end = str.find_last_not_of("\r\n\t ");
  EXPECT_EQ((end == std::string::npos) ? "" : str.substr(0, end + 1), expected);
}
