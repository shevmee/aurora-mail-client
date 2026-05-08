#include <gtest/gtest.h>

#include "EnhancedCode.hpp"

using namespace aurora::mail::smtp::response;

// ---------- Happy path ----------

TEST(EnhancedCodeParse, ParsesSuccessCode)
{
  const auto ec = EnhancedCode::parse("2.0.0");
  ASSERT_TRUE(ec.has_value());
  EXPECT_EQ(ec->class_code, ClassCode::Success);
  EXPECT_EQ(ec->subject, SubjectCode::Undefined);
  EXPECT_EQ(ec->detail, 0);
  EXPECT_TRUE(ec->isSuccess());
  EXPECT_FALSE(ec->isTransientFailure());
  EXPECT_FALSE(ec->isPermanentFailure());
}

TEST(EnhancedCodeParse, ParsesTransientFailure)
{
  const auto ec = EnhancedCode::parse("4.2.2");
  ASSERT_TRUE(ec.has_value());
  EXPECT_EQ(ec->class_code, ClassCode::PersistentTransientFailure);
  EXPECT_EQ(ec->subject, SubjectCode::Mailbox);
  EXPECT_EQ(ec->detail, 2);
  EXPECT_TRUE(ec->isTransientFailure());
  EXPECT_FALSE(ec->isSuccess());
  EXPECT_FALSE(ec->isPermanentFailure());
}

TEST(EnhancedCodeParse, ParsesPermanentFailure)
{
  const auto ec = EnhancedCode::parse("5.7.1");
  ASSERT_TRUE(ec.has_value());
  EXPECT_EQ(ec->class_code, ClassCode::PersistentFailure);
  EXPECT_EQ(ec->subject, SubjectCode::Security);
  EXPECT_EQ(ec->detail, 1);
  EXPECT_TRUE(ec->isPermanentFailure());
}

TEST(EnhancedCodeParse, ToStringRoundTrip)
{
  for (const auto* s : { "2.0.0", "4.2.2", "5.7.1", "2.1.5", "5.3.4" })
  {
    const auto ec = EnhancedCode::parse(s);
    ASSERT_TRUE(ec.has_value()) << s;
    EXPECT_EQ(ec->toString(), s);
  }
}

// ---------- Bug we just fixed: separator validation ----------

TEST(EnhancedCodeParse, RejectsNonDotSeparators)
{
  EXPECT_FALSE(EnhancedCode::parse("5x7y1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5,7,1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5-7-1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5 7 1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.7,1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5,7.1").has_value());
}

// ---------- Other invalid inputs ----------

TEST(EnhancedCodeParse, RejectsEmptyString)
{
  EXPECT_FALSE(EnhancedCode::parse("").has_value());
}

TEST(EnhancedCodeParse, RejectsNonNumericInput)
{
  EXPECT_FALSE(EnhancedCode::parse("abc").has_value());
  EXPECT_FALSE(EnhancedCode::parse("a.b.c").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.x.1").has_value());
}

TEST(EnhancedCodeParse, RejectsTrailingCharacters)
{
  EXPECT_FALSE(EnhancedCode::parse("5.7.1x").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.7.1 ").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.7.1.2").has_value());
}

TEST(EnhancedCodeParse, RejectsMissingComponents)
{
  EXPECT_FALSE(EnhancedCode::parse("5").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.7").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.7.").has_value());
  EXPECT_FALSE(EnhancedCode::parse(".7.1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5..1").has_value());
}

TEST(EnhancedCodeParse, RejectsInvalidClass)
{
  // RFC 3463: class must be 2, 4, or 5.
  EXPECT_FALSE(EnhancedCode::parse("0.0.0").has_value());
  EXPECT_FALSE(EnhancedCode::parse("1.0.0").has_value());
  EXPECT_FALSE(EnhancedCode::parse("3.0.0").has_value());
  EXPECT_FALSE(EnhancedCode::parse("6.0.0").has_value());
  EXPECT_FALSE(EnhancedCode::parse("9.0.0").has_value());
}

TEST(EnhancedCodeParse, RejectsOutOfRangeSubjectOrDetail)
{
  // Subject and detail must be single digits 0-9.
  EXPECT_FALSE(EnhancedCode::parse("5.10.1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.7.10").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.99.1").has_value());
}

TEST(EnhancedCodeParse, RejectsNegativeNumbers)
{
  EXPECT_FALSE(EnhancedCode::parse("-5.7.1").has_value());
  EXPECT_FALSE(EnhancedCode::parse("5.-7.1").has_value());
}

// ---------- Predicates ----------

TEST(EnhancedCodeParse, EqualityOperators)
{
  const auto a = EnhancedCode::parse("5.7.1");
  const auto b = EnhancedCode::parse("5.7.1");
  const auto c = EnhancedCode::parse("5.7.2");

  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_TRUE(c.has_value());

  EXPECT_EQ(*a, *b);
  EXPECT_NE(*a, *c);
}
