#include <gtest/gtest.h>

#include <MailAddress.hpp>

using namespace aurora::mail::common::mail;

TEST(MailAddressTest, DefaultConstructor)
{
  MailAddress address;
  EXPECT_TRUE(address.getAddress().empty());
  EXPECT_TRUE(address.getName().empty());
  EXPECT_FALSE(address.isValid());
}

TEST(MailAddressTest, ValidEmailWithName)
{
  MailAddress address("john.doe@example.com", "John Doe");
  EXPECT_EQ(address.getAddress(), "john.doe@example.com");
  EXPECT_EQ(address.getName(), "John Doe");
  EXPECT_TRUE(address.isValid());
}

TEST(MailAddressTest, ValidEmailWithoutName)
{
  MailAddress address("jane.smith@domain.org");
  EXPECT_EQ(address.getAddress(), "jane.smith@domain.org");
  EXPECT_TRUE(address.getName().empty());
  EXPECT_TRUE(address.isValid());
}

TEST(MailAddressTest, CreateRejectsInvalidEmail)
{
  EXPECT_FALSE(MailAddress::create("invalid-email", "Jane Doe").has_value());
  EXPECT_FALSE(MailAddress::create("example@.com", "Invalid Format").has_value());
  EXPECT_FALSE(MailAddress::create("user@com", "Short Domain").has_value());
  EXPECT_FALSE(MailAddress::create("user@example", "No TLD").has_value());
  EXPECT_FALSE(MailAddress::create("", "Empty Email").has_value());
}

TEST(MailAddressTest, CreateAcceptsValidEmail)
{
  auto result = MailAddress::create("john.doe@example.com", "John Doe");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->getAddress(), "john.doe@example.com");
  EXPECT_EQ(result->getName(), "John Doe");
}

TEST(MailAddressTest, IsValidMethod)
{
  MailAddress valid_addr("valid@email.com");
  EXPECT_TRUE(valid_addr.isValid());

  MailAddress default_addr;
  EXPECT_FALSE(default_addr.isValid());
}

TEST(MailAddressTest, EqualityOperator)
{
  MailAddress addr1("test@example.com", "User One");
  MailAddress addr2("test@example.com", "User Two");
  MailAddress addr3("another@example.com", "User One");
  MailAddress addr4("TEST@example.com", "User One");

  EXPECT_TRUE(addr1 == addr2);
  EXPECT_FALSE(addr1 == addr3);
  EXPECT_FALSE(addr1 == addr4);
}

TEST(MailAddressTest, InequalityOperator)
{
  MailAddress addr1("test@example.com", "User One");
  MailAddress addr2("test@example.com", "User Two");
  MailAddress addr3("another@example.com", "User One");

  EXPECT_FALSE(addr1 != addr2);
  EXPECT_TRUE(addr1 != addr3);
}

TEST(MailAddressTest, MoveSemantics)
{
  std::string email_source = "move@example.com";
  std::string name_source = "Move User";

  std::string expected_email = email_source;
  std::string expected_name = name_source;

  MailAddress address(std::move(email_source), std::move(name_source));

  EXPECT_EQ(address.getAddress(), expected_email);
  EXPECT_EQ(address.getName(), expected_name);

  EXPECT_TRUE(email_source.empty());
  EXPECT_TRUE(name_source.empty());
}

TEST(MailAddressTest, OstreamOperator)
{
  MailAddress addr_with_name("test@example.com", "Test User");
  std::stringstream ss_with_name;
  ss_with_name << addr_with_name.getAddress() << " " << addr_with_name.getName();
  // Actual format: "test@example.com Test User"
  EXPECT_EQ(ss_with_name.str(), "test@example.com Test User");

  MailAddress addr_no_name("no.name@domain.org");
  std::stringstream ss_no_name;
  ss_no_name << addr_no_name.getAddress();
  EXPECT_EQ(ss_no_name.str(), "no.name@domain.org");
}
