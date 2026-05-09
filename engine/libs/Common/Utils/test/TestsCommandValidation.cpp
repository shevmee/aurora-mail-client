#include <gtest/gtest.h>

#include <CommandValidation.hpp>

using aurora::mail::common::ProtocolError;
using aurora::mail::common::validateNoCrlf;

TEST(CommandValidation, EmptyStringIsAccepted)
{
  auto r = validateNoCrlf("", "field");
  EXPECT_TRUE(r.has_value());
}

TEST(CommandValidation, PrintableAsciiIsAccepted)
{
  auto r = validateNoCrlf("user@example.com", "RCPT TO");
  EXPECT_TRUE(r.has_value());
}

TEST(CommandValidation, RejectsCarriageReturn)
{
  auto r = validateNoCrlf("foo\rbar", "x");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().category, ProtocolError::Category::PROTOCOL);
}

TEST(CommandValidation, RejectsLineFeed)
{
  auto r = validateNoCrlf("foo\nbar", "x");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().category, ProtocolError::Category::PROTOCOL);
}

TEST(CommandValidation, RejectsCrlfPair)
{
  auto r = validateNoCrlf("INJECTED\r\nMAIL FROM:<x@y>", "subject");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().category, ProtocolError::Category::PROTOCOL);
}

TEST(CommandValidation, RejectsEmbeddedNul)
{
  auto r = validateNoCrlf(std::string_view("foo\0bar", 7), "x");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().category, ProtocolError::Category::PROTOCOL);
}

TEST(CommandValidation, ErrorMessageIncludesFieldNameAndOffset)
{
  auto r = validateNoCrlf("ab\rcd", "myField");
  ASSERT_FALSE(r.has_value());
  // Field name should appear in the message; offset should be 2 (zero-based).
  EXPECT_NE(r.error().message.find("myField"), std::string::npos);
  EXPECT_NE(r.error().message.find("2"), std::string::npos);
}

TEST(CommandValidation, AcceptsTabAndOtherControlChars)
{
  // Only CR/LF/NUL are forbidden; embedded tabs are allowed even though many
  // commands don't actually accept them. The validator's job is narrow.
  EXPECT_TRUE(validateNoCrlf("a\tb", "x").has_value());
  EXPECT_TRUE(validateNoCrlf("\x7f", "x").has_value());
}

TEST(CommandValidation, AcceptsLongValidString)
{
  std::string s(10000, 'A');
  EXPECT_TRUE(validateNoCrlf(s, "x").has_value());
}
