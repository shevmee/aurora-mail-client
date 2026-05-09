#include <gtest/gtest.h>

#include <ProtocolError.hpp>

using aurora::mail::common::ProtocolError;

TEST(ProtocolError, ConstructorPopulatesAllFields)
{
  ProtocolError err{ ProtocolError::Category::IO, "boom", "details" };
  EXPECT_EQ(err.category, ProtocolError::Category::IO);
  EXPECT_EQ(err.message, "boom");
  EXPECT_EQ(err.details, "details");
}

TEST(ProtocolError, FactoryMethodsSetCategoryCorrectly)
{
  EXPECT_EQ(ProtocolError::connection("x").category, ProtocolError::Category::CONNECTION);
  EXPECT_EQ(ProtocolError::io("x").category, ProtocolError::Category::IO);
  EXPECT_EQ(ProtocolError::protocol("x").category, ProtocolError::Category::PROTOCOL);
  EXPECT_EQ(ProtocolError::auth("x").category, ProtocolError::Category::AUTHENTICATION);
  EXPECT_EQ(ProtocolError::timeout("x").category, ProtocolError::Category::TIMEOUT);
  EXPECT_EQ(ProtocolError::tls("x").category, ProtocolError::Category::TLS);
  EXPECT_EQ(ProtocolError::invalidState("x").category, ProtocolError::Category::INVALID_STATE);
  EXPECT_EQ(ProtocolError::cancelled("x").category, ProtocolError::Category::CANCELLED);
}

TEST(ProtocolError, CategoryToStringMatchesEnum)
{
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::CONNECTION), "CONNECTION");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::IO), "IO");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::PROTOCOL), "PROTOCOL");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::AUTHENTICATION), "AUTHENTICATION");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::TIMEOUT), "TIMEOUT");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::TLS), "TLS");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::INVALID_STATE), "INVALID_STATE");
  EXPECT_STREQ(ProtocolError::categoryToString(ProtocolError::Category::CANCELLED), "CANCELLED");
}

TEST(ProtocolError, ToStringIncludesCategoryAndMessage)
{
  auto str = ProtocolError::io("read failed").toString();
  EXPECT_NE(str.find("IO"), std::string::npos);
  EXPECT_NE(str.find("read failed"), std::string::npos);
}

TEST(ProtocolError, ToStringIncludesDetailsWhenPresent)
{
  auto str = ProtocolError::io("read failed", "ECONNRESET").toString();
  EXPECT_NE(str.find("read failed"), std::string::npos);
  EXPECT_NE(str.find("ECONNRESET"), std::string::npos);
}

TEST(ProtocolError, ToStringOmitsParenthesesWhenNoDetails)
{
  auto str = ProtocolError::io("read failed").toString();
  EXPECT_EQ(str.find('('), std::string::npos) << "details parenthesis must be omitted when details are empty: " << str;
}
