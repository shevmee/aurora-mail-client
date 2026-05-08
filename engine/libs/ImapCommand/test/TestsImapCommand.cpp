#include <gtest/gtest.h>

#include "ImapCommand.hpp"
#include "TagGenerator.hpp"

using namespace aurora::mail::imap::command;
using aurora::mail::common::TagGenerator;

namespace
{
  // Convenience for tests where we expect serialize() to succeed.
  std::string ok(auto&& cmd)
  {
    auto r = cmd.serialize();
    EXPECT_TRUE(r.has_value());
    return r.value();
  }
}  // namespace

TEST(ImapCommandTest, Capability_Serialization)
{
  Capability cmd{ "A001" };

  EXPECT_EQ(ok(cmd), "A001 CAPABILITY\r\n");
  EXPECT_EQ(Capability::name(), "CAPABILITY");
}

TEST(ImapCommandTest, Login_Serialization)
{
  Login cmd{ "A002", "john.doe", "secret123" };

  EXPECT_EQ(ok(cmd), "A002 LOGIN \"john.doe\" \"secret123\"\r\n");
  EXPECT_EQ(Login::name(), "LOGIN");
}

TEST(ImapCommandTest, Login_WithSpecialChars)
{
  Login cmd{ "TAG1", "user@example.com", "p@ss\"word" };

  // Note: serialize() does not currently escape embedded quotes; that's a
  // higher-level concern, but the function should still produce something.
  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with("TAG1 LOGIN "));
  EXPECT_TRUE(result.ends_with("\r\n"));
}

TEST(ImapCommandTest, Logout_Serialization)
{
  Logout cmd{ "A003" };

  EXPECT_EQ(ok(cmd), "A003 LOGOUT\r\n");
  EXPECT_EQ(Logout::name(), "LOGOUT");
}

TEST(ImapCommandTest, Select_Serialization)
{
  Select cmd{ "A004", "INBOX" };

  EXPECT_EQ(ok(cmd), "A004 SELECT INBOX\r\n");
  EXPECT_EQ(Select::name(), "SELECT");
}

TEST(ImapCommandTest, Select_CustomMailbox)
{
  Select cmd{ "A005", "Sent Items" };

  // "Sent Items" needs quoting (contains space).
  EXPECT_EQ(ok(cmd), "A005 SELECT \"Sent Items\"\r\n");
}

TEST(ImapCommandTest, Examine_Serialization)
{
  Examine cmd{ "A006", "INBOX" };

  EXPECT_EQ(ok(cmd), "A006 EXAMINE INBOX\r\n");
  EXPECT_EQ(Examine::name(), "EXAMINE");
}

TEST(ImapCommandTest, Fetch_Serialization)
{
  Fetch cmd{ "A007", "42", "BODY[]" };

  EXPECT_EQ(ok(cmd), "A007 FETCH 42 BODY[]\r\n");
  EXPECT_EQ(Fetch::name(), "FETCH");
}

TEST(ImapCommandTest, Fetch_RFC822)
{
  Fetch cmd{ "A008", "100", "RFC822" };

  EXPECT_EQ(ok(cmd), "A008 FETCH 100 RFC822\r\n");
}

TEST(ImapCommandTest, Fetch_Flags)
{
  Fetch cmd{ "A009", "1", "FLAGS" };

  EXPECT_EQ(ok(cmd), "A009 FETCH 1 FLAGS\r\n");
}

TEST(ImapCommandTest, Store_Serialization)
{
  Store cmd{ "A010", "5", "+FLAGS (\\Seen)" };

  EXPECT_EQ(ok(cmd), "A010 STORE 5 +FLAGS (\\Seen)\r\n");
  EXPECT_EQ(Store::name(), "STORE");
}

TEST(ImapCommandTest, Store_RemoveFlags)
{
  Store cmd{ "A011", "10", "-FLAGS (\\Flagged)" };

  EXPECT_EQ(ok(cmd), "A011 STORE 10 -FLAGS (\\Flagged)\r\n");
}

TEST(ImapCommandTest, Search_Serialization)
{
  Search cmd{ "A012", "UNSEEN" };

  EXPECT_EQ(ok(cmd), "A012 SEARCH UNSEEN\r\n");
  EXPECT_EQ(Search::name(), "SEARCH");
}

TEST(ImapCommandTest, Search_Complex)
{
  Search cmd{ "A013", "FROM \"john@example.com\" SINCE 1-Jan-2024" };

  EXPECT_EQ(ok(cmd), "A013 SEARCH FROM \"john@example.com\" SINCE 1-Jan-2024\r\n");
}

TEST(ImapCommandTest, List_Serialization)
{
  List cmd{ "A014", "", "*" };

  EXPECT_EQ(ok(cmd), "A014 LIST \"\" \"*\"\r\n");
  EXPECT_EQ(List::name(), "LIST");
}

TEST(ImapCommandTest, List_WithReference)
{
  List cmd{ "A015", "INBOX/", "%" };

  EXPECT_EQ(ok(cmd), "A015 LIST \"INBOX/\" \"%\"\r\n");
}

TEST(ImapCommandTest, Noop_Serialization)
{
  Noop cmd{ "A016" };

  EXPECT_EQ(ok(cmd), "A016 NOOP\r\n");
  EXPECT_EQ(Noop::name(), "NOOP");
}

TEST(ImapCommandTest, StartTls_Serialization)
{
  StartTls cmd{ "A017" };

  EXPECT_EQ(ok(cmd), "A017 STARTTLS\r\n");
  EXPECT_EQ(StartTls::name(), "STARTTLS");
}

TEST(ImapCommandTest, DifferentTagFormats)
{
  EXPECT_EQ(ok(Capability{ "A001" }), "A001 CAPABILITY\r\n");
  EXPECT_EQ(ok(Capability{ "TAG123" }), "TAG123 CAPABILITY\r\n");
  EXPECT_EQ(ok(Capability{ "xyz" }), "xyz CAPABILITY\r\n");
  EXPECT_EQ(ok(Capability{ "1" }), "1 CAPABILITY\r\n");
}

TEST(ImapCommandTest, AuthPlain_Serialization)
{
  AuthPlain cmd{ "A100", "user@example.com", "secret" };
  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with("A100 AUTHENTICATE PLAIN "));
  EXPECT_TRUE(result.ends_with("\r\n"));
  EXPECT_EQ(AuthPlain::name(), "AUTHENTICATE PLAIN");
}

TEST(ImapCommandTest, AuthXOAuth2_WithToken)
{
  AuthXOAuth2 cmd{ "A101", "user@example.com", "ya29.test_token" };
  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with("A101 AUTHENTICATE XOAUTH2 "));
  EXPECT_TRUE(result.ends_with("\r\n"));
  EXPECT_EQ(AuthXOAuth2::name(), "AUTHENTICATE XOAUTH2");
}

TEST(ImapCommandTest, AuthXOAuth2_EmptyToken_Fails)
{
  AuthXOAuth2 cmd{ "A102", "user@example.com", "" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().category, aurora::mail::common::ProtocolError::Category::AUTHENTICATION);
}

TEST(ImapCommandTest, Login_RejectsCrInPassword)
{
  Login cmd{ "A001", "user", "pass\r\nA002 SELECT \"INBOX/admin\"" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().category, aurora::mail::common::ProtocolError::Category::PROTOCOL);
}

TEST(ImapCommandTest, Select_RejectsLfInMailbox)
{
  Select cmd{ "A001", "INBOX\nA002 LOGOUT" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(ImapCommandTest, Capability_RejectsCrInTag)
{
  Capability cmd{ "A0\r01" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(ImapCommandTest, Search_RejectsNulInCriteria)
{
  Search cmd{ "A001", std::string{ "ALL" } + char{ 0 } + "EXTRA" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(ImapCommandTest, TagGenerator_Sequential)
{
  TagGenerator gen;

  EXPECT_EQ(gen.next(), "A001");
  EXPECT_EQ(gen.next(), "A002");
  EXPECT_EQ(gen.next(), "A003");
}

TEST(ImapCommandTest, TagGenerator_Reset)
{
  TagGenerator gen;

  (void)gen.next();
  (void)gen.next();
  gen.reset();

  EXPECT_EQ(gen.next(), "A001");
}

TEST(ImapCommandTest, TagGenerator_ManyTags)
{
  TagGenerator gen;

  for (int i = 1; i <= 100; ++i)
  {
    (void)gen.next();
  }

  EXPECT_EQ(gen.next(), "A101");
}

TEST(ImapCommandTest, Serializable_Concept)
{
  static_assert(aurora::mail::common::Serializable<Capability>);
  static_assert(aurora::mail::common::Serializable<Login>);
  static_assert(aurora::mail::common::Serializable<Logout>);
  static_assert(aurora::mail::common::Serializable<Select>);
  static_assert(aurora::mail::common::Serializable<Examine>);
  static_assert(aurora::mail::common::Serializable<Fetch>);
  static_assert(aurora::mail::common::Serializable<Store>);
  static_assert(aurora::mail::common::Serializable<Search>);
  static_assert(aurora::mail::common::Serializable<List>);
  static_assert(aurora::mail::common::Serializable<Noop>);
  static_assert(aurora::mail::common::Serializable<StartTls>);
  static_assert(aurora::mail::common::Serializable<AuthPlain>);
  static_assert(aurora::mail::common::Serializable<AuthXOAuth2>);
  static_assert(aurora::mail::common::Serializable<Rename>);
  static_assert(aurora::mail::common::Serializable<Subscribe>);
  static_assert(aurora::mail::common::Serializable<Unsubscribe>);
  static_assert(aurora::mail::common::Serializable<Lsub>);
  static_assert(aurora::mail::common::Serializable<Append>);
  static_assert(aurora::mail::common::Serializable<UidMove>);
  static_assert(aurora::mail::common::Serializable<UidFetch>);
  static_assert(aurora::mail::common::Serializable<UidStore>);
  static_assert(aurora::mail::common::Serializable<UidSearch>);
  static_assert(aurora::mail::common::Serializable<UidCopy>);
  static_assert(aurora::mail::common::Serializable<UidExpunge>);
  static_assert(aurora::mail::common::Serializable<Status>);
  static_assert(aurora::mail::common::Serializable<Expunge>);
  static_assert(aurora::mail::common::Serializable<Close>);
  static_assert(aurora::mail::common::Serializable<Idle>);
  static_assert(aurora::mail::common::Serializable<SelectCondstore>);
  static_assert(aurora::mail::common::Serializable<ExamineCondstore>);
  static_assert(aurora::mail::common::Serializable<UidFetchChangedSince>);
  static_assert(aurora::mail::common::Serializable<UidStoreUnchangedSince>);
  static_assert(aurora::mail::common::Serializable<SelectQresync>);
  static_assert(aurora::mail::common::Serializable<EnableQresync>);
  static_assert(aurora::mail::common::Serializable<Create>);
  static_assert(aurora::mail::common::Serializable<Delete>);
}

TEST(ImapCommandTest, ProtocolCommand_Concept)
{
  static_assert(aurora::mail::common::ProtocolCommand<Capability>);
  static_assert(aurora::mail::common::ProtocolCommand<Login>);
  static_assert(aurora::mail::common::ProtocolCommand<Logout>);
  static_assert(aurora::mail::common::ProtocolCommand<Select>);
  static_assert(aurora::mail::common::ProtocolCommand<Examine>);
  static_assert(aurora::mail::common::ProtocolCommand<Fetch>);
  static_assert(aurora::mail::common::ProtocolCommand<Store>);
  static_assert(aurora::mail::common::ProtocolCommand<Search>);
  static_assert(aurora::mail::common::ProtocolCommand<List>);
  static_assert(aurora::mail::common::ProtocolCommand<Noop>);
  static_assert(aurora::mail::common::ProtocolCommand<StartTls>);
}

TEST(ImapCommandTest, CommandVariant_Serialize)
{
  Command cmd1 = Capability{ "A001" };
  EXPECT_EQ(serialize(cmd1).value(), "A001 CAPABILITY\r\n");

  Command cmd2 = Login{ "A002", "user", "pass" };
  EXPECT_EQ(serialize(cmd2).value(), "A002 LOGIN \"user\" \"pass\"\r\n");

  Command cmd3 = Logout{ "A003" };
  EXPECT_EQ(serialize(cmd3).value(), "A003 LOGOUT\r\n");
}

TEST(ImapCommandTest, IndividualCommand_Serialize)
{
  Select cmd{ "TAG1", "INBOX" };
  EXPECT_EQ(serialize(cmd).value(), "TAG1 SELECT INBOX\r\n");
}

TEST(ImapCommandTest, AllCommandsHaveCRLF)
{
  EXPECT_TRUE(ok(Capability{ "T" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Login{ "T", "u", "p" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Logout{ "T" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Select{ "T", "INBOX" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Examine{ "T", "INBOX" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Fetch{ "T", "1", "BODY[]" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Store{ "T", "1", "FLAGS" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Search{ "T", "ALL" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(List{ "T", "", "*" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Noop{ "T" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(StartTls{ "T" }).ends_with("\r\n"));
}

TEST(ImapCommandTest, EmptyTag)
{
  Capability cmd{ "" };
  EXPECT_EQ(ok(cmd), " CAPABILITY\r\n");
}

TEST(ImapCommandTest, LongTag)
{
  std::string long_tag(100, 'A');
  Capability cmd{ long_tag };

  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with(long_tag));
  EXPECT_TRUE(result.ends_with(" CAPABILITY\r\n"));
}

TEST(ImapCommandTest, UnicodeInMailboxName)
{
  // Emoji bytes are non-ASCII but contain no CR/LF/NUL, so the validator
  // accepts them; quoteMailbox quotes them because they fall outside ASCII.
  Select cmd{ "A001", "INBOX-Mail" };

  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with("A001 SELECT "));
  EXPECT_TRUE(result.ends_with("\r\n"));
}

TEST(ImapCommandTest, LargeMessageSet)
{
  Fetch cmd{ "A001", "999999", "BODY[]" };

  EXPECT_EQ(ok(cmd), "A001 FETCH 999999 BODY[]\r\n");
}

TEST(ImapCommandTest, ComplexSearchCriteria)
{
  Search cmd{ "A001", "OR UNSEEN FROM \"test@example.com\"" };

  EXPECT_EQ(ok(cmd), "A001 SEARCH OR UNSEEN FROM \"test@example.com\"\r\n");
}

TEST(ImapCommandTest, TypicalSession)
{
  TagGenerator gen;

  Capability cap{ gen.next() };
  EXPECT_EQ(ok(cap), "A001 CAPABILITY\r\n");

  Login login{ gen.next(), "user@example.com", "password" };
  EXPECT_EQ(ok(login), "A002 LOGIN \"user@example.com\" \"password\"\r\n");

  Select select{ gen.next(), "INBOX" };
  EXPECT_EQ(ok(select), "A003 SELECT INBOX\r\n");

  Search search{ gen.next(), "UNSEEN" };
  EXPECT_EQ(ok(search), "A004 SEARCH UNSEEN\r\n");

  Fetch fetch{ gen.next(), "1", "BODY[]" };
  EXPECT_EQ(ok(fetch), "A005 FETCH 1 BODY[]\r\n");

  Store store{ gen.next(), "1", "+FLAGS (\\Seen)" };
  EXPECT_EQ(ok(store), "A006 STORE 1 +FLAGS (\\Seen)\r\n");

  Logout logout{ gen.next() };
  EXPECT_EQ(ok(logout), "A007 LOGOUT\r\n");
}
