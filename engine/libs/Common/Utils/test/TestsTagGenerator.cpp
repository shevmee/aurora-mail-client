#include <gtest/gtest.h>

#include <TagGenerator.hpp>

using aurora::mail::common::TagGenerator;

TEST(TagGenerator, DefaultPrefixIsCapitalA)
{
  TagGenerator gen;
  EXPECT_EQ(gen.next(), "A001");
  EXPECT_EQ(gen.next(), "A002");
}

TEST(TagGenerator, CustomPrefixUsed)
{
  TagGenerator gen("TAG");
  EXPECT_EQ(gen.next(), "TAG001");
  EXPECT_EQ(gen.next(), "TAG002");
  EXPECT_EQ(gen.next(), "TAG003");
}

TEST(TagGenerator, CounterIsZeroPaddedToThreeDigits)
{
  TagGenerator gen;
  EXPECT_EQ(gen.next(), "A001");
  for (int i = 2; i <= 9; ++i)
  {
    auto tag = gen.next();
    EXPECT_EQ(tag.size(), 4U);
    EXPECT_EQ(tag[0], 'A');
  }
}

TEST(TagGenerator, GrowsBeyond999Digits)
{
  TagGenerator gen;
  for (int i = 0; i < 999; ++i)
    (void)gen.next();
  // Counter is now at 1000; format("{:03}") should produce 4-digit number.
  EXPECT_EQ(gen.next(), "A1000");
  EXPECT_EQ(gen.next(), "A1001");
}

TEST(TagGenerator, ResetReturnsToOneByDefault)
{
  TagGenerator gen;
  (void)gen.next();
  (void)gen.next();
  gen.reset();
  EXPECT_EQ(gen.next(), "A001");
}

TEST(TagGenerator, ResetWithExplicitStart)
{
  TagGenerator gen;
  gen.reset(42);
  EXPECT_EQ(gen.next(), "A042");
  EXPECT_EQ(gen.next(), "A043");
}

TEST(TagGenerator, EmptyPrefixIsAllowed)
{
  TagGenerator gen("");
  EXPECT_EQ(gen.next(), "001");
}

TEST(TagGenerator, IndependentInstancesHaveIndependentCounters)
{
  TagGenerator a("A");
  TagGenerator b("B");
  EXPECT_EQ(a.next(), "A001");
  EXPECT_EQ(b.next(), "B001");
  EXPECT_EQ(a.next(), "A002");
  EXPECT_EQ(b.next(), "B002");
}
