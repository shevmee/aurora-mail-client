#include <gtest/gtest.h>

#include <SmtpCommand.hpp>

using namespace aurora::mail::smtp::command;

namespace
{
  // Convenience: assert serialize() succeeds and return the string. Tests
  // failing at this assertion mean the command rejected its inputs (e.g. CRLF
  // injection guard tripped) when it shouldn't have.
  std::string ok(auto&& cmd)
  {
    auto r = cmd.serialize();
    EXPECT_TRUE(r.has_value());
    return r.value();
  }
}  // namespace

TEST(SmtpCommandTest, Helo_Serialization)
{
  Helo cmd{ "example.com" };

  EXPECT_EQ(ok(cmd), "HELO example.com\r\n");
  EXPECT_EQ(Helo::name(), "HELO");
}

TEST(SmtpCommandTest, Ehlo_Serialization)
{
  Ehlo cmd{ "mail.example.com" };

  EXPECT_EQ(ok(cmd), "EHLO mail.example.com\r\n");
  EXPECT_EQ(Ehlo::name(), "EHLO");
}

TEST(SmtpCommandTest, MailFrom_Serialization)
{
  MailFrom cmd{ "sender@example.com" };

  EXPECT_EQ(ok(cmd), "MAIL FROM:<sender@example.com>\r\n");
  EXPECT_EQ(MailFrom::name(), "MAIL FROM");
}

TEST(SmtpCommandTest, MailFrom_WithSpecialChars)
{
  MailFrom cmd{ "test+123@sub.example.com" };

  EXPECT_EQ(ok(cmd), "MAIL FROM:<test+123@sub.example.com>\r\n");
}

TEST(SmtpCommandTest, RcptTo_Serialization)
{
  RcptTo cmd{ "recipient@example.com" };

  EXPECT_EQ(ok(cmd), "RCPT TO:<recipient@example.com>\r\n");
  EXPECT_EQ(RcptTo::name(), "RCPT TO");
}

TEST(SmtpCommandTest, Data_Serialization)
{
  Data cmd;

  EXPECT_EQ(ok(cmd), "DATA\r\n");
  EXPECT_EQ(Data::name(), "DATA");
}

TEST(SmtpCommandTest, Quit_Serialization)
{
  Quit cmd;

  EXPECT_EQ(ok(cmd), "QUIT\r\n");
  EXPECT_EQ(Quit::name(), "QUIT");
}

TEST(SmtpCommandTest, Rset_Serialization)
{
  Rset cmd;

  EXPECT_EQ(ok(cmd), "RSET\r\n");
  EXPECT_EQ(Rset::name(), "RSET");
}

TEST(SmtpCommandTest, StartTls_Serialization)
{
  StartTls cmd;

  EXPECT_EQ(ok(cmd), "STARTTLS\r\n");
  EXPECT_EQ(StartTls::name(), "STARTTLS");
}

TEST(SmtpCommandTest, Noop_NoArg)
{
  Noop cmd{ "" };

  EXPECT_EQ(ok(cmd), "NOOP\r\n");
  EXPECT_EQ(Noop::name(), "NOOP");
}

TEST(SmtpCommandTest, Noop_WithArg)
{
  Noop cmd{ "test argument" };

  EXPECT_EQ(ok(cmd), "NOOP test argument\r\n");
}

TEST(SmtpCommandTest, Vrfy_Serialization)
{
  Vrfy cmd{ "user@example.com" };

  EXPECT_EQ(ok(cmd), "VRFY user@example.com\r\n");
  EXPECT_EQ(Vrfy::name(), "VRFY");
}

TEST(SmtpCommandTest, Help_NoArg)
{
  Help cmd{ "" };

  EXPECT_EQ(ok(cmd), "HELP\r\n");
  EXPECT_EQ(Help::name(), "HELP");
}

TEST(SmtpCommandTest, Help_WithArg)
{
  Help cmd{ "MAIL" };

  EXPECT_EQ(ok(cmd), "HELP MAIL\r\n");
}

TEST(SmtpCommandTest, AuthPlain_Serialization)
{
  AuthPlain cmd{ "testuser", "testpass" };

  auto result = ok(cmd);

  EXPECT_TRUE(result.starts_with("AUTH PLAIN "));
  EXPECT_TRUE(result.ends_with("\r\n"));
  EXPECT_EQ(AuthPlain::name(), "AUTH PLAIN");
  EXPECT_GT(result.length(), 20);
}

TEST(SmtpCommandTest, AuthPlain_EmptyCredentials)
{
  AuthPlain cmd{ "", "" };

  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with("AUTH PLAIN "));
}

TEST(SmtpCommandTest, AuthLogin_Serialization)
{
  AuthLogin cmd{ "testuser", "testpass" };

  // LOGIN sends initial command only; SmtpClient::asyncAuthenticate handles
  // the multi-step challenge/response flow.
  EXPECT_EQ(ok(cmd), "AUTH LOGIN\r\n");
  EXPECT_EQ(AuthLogin::name(), "AUTH LOGIN");
}

TEST(SmtpCommandTest, AuthXOAuth2_WithToken)
{
  AuthXOAuth2 cmd{ "user@example.com", "ya29.test_token_here" };

  auto result = ok(cmd);

  EXPECT_TRUE(result.starts_with("AUTH XOAUTH2 "));
  EXPECT_TRUE(result.ends_with("\r\n"));
  EXPECT_EQ(AuthXOAuth2::name(), "AUTH XOAUTH2");
  EXPECT_GT(result.length(), 25);
}

TEST(SmtpCommandTest, AuthXOAuth2_EmptyToken_ReturnsError)
{
  AuthXOAuth2 cmd{ "user@example.com", "" };

  auto result = cmd.serialize();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().category, aurora::mail::common::ProtocolError::Category::AUTHENTICATION);
}

TEST(SmtpCommandTest, Helo_RejectsCrInDomain)
{
  Helo cmd{ "evil.com\r\nRCPT TO:<victim@example.com>" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().category, aurora::mail::common::ProtocolError::Category::PROTOCOL);
}

TEST(SmtpCommandTest, MailFrom_RejectsLfInAddress)
{
  MailFrom cmd{ "user@example.com>\nRCPT TO:<extra@evil" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(SmtpCommandTest, RcptTo_RejectsNulInAddress)
{
  RcptTo cmd{ std::string{ "u@e.com" } + char{ 0 } + "trailing" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(SmtpCommandTest, AuthPlain_RejectsCrInPassword)
{
  AuthPlain cmd{ "user", "pass\r\nQUIT" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(SmtpCommandTest, AuthXOAuth2_RejectsLfInToken)
{
  AuthXOAuth2 cmd{ "user@x", "tok\nINJECT" };
  auto result = cmd.serialize();
  ASSERT_FALSE(result.has_value());
}

TEST(SmtpCommandTest, Serializable_Concept)
{
  static_assert(aurora::mail::common::Serializable<Helo>);
  static_assert(aurora::mail::common::Serializable<Ehlo>);
  static_assert(aurora::mail::common::Serializable<MailFrom>);
  static_assert(aurora::mail::common::Serializable<RcptTo>);
  static_assert(aurora::mail::common::Serializable<Data>);
  static_assert(aurora::mail::common::Serializable<Quit>);
  static_assert(aurora::mail::common::Serializable<Rset>);
  static_assert(aurora::mail::common::Serializable<StartTls>);
  static_assert(aurora::mail::common::Serializable<Noop>);
  static_assert(aurora::mail::common::Serializable<Vrfy>);
  static_assert(aurora::mail::common::Serializable<Help>);
  static_assert(aurora::mail::common::Serializable<AuthPlain>);
  static_assert(aurora::mail::common::Serializable<AuthLogin>);
  static_assert(aurora::mail::common::Serializable<AuthXOAuth2>);
}

TEST(SmtpCommandTest, ProtocolCommand_Concept)
{
  static_assert(aurora::mail::common::ProtocolCommand<Helo>);
  static_assert(aurora::mail::common::ProtocolCommand<Ehlo>);
  static_assert(aurora::mail::common::ProtocolCommand<MailFrom>);
  static_assert(aurora::mail::common::ProtocolCommand<RcptTo>);
  static_assert(aurora::mail::common::ProtocolCommand<Data>);
  static_assert(aurora::mail::common::ProtocolCommand<Quit>);
  static_assert(aurora::mail::common::ProtocolCommand<Rset>);
  static_assert(aurora::mail::common::ProtocolCommand<StartTls>);
  static_assert(aurora::mail::common::ProtocolCommand<Noop>);
  static_assert(aurora::mail::common::ProtocolCommand<Vrfy>);
  static_assert(aurora::mail::common::ProtocolCommand<Help>);
  static_assert(aurora::mail::common::ProtocolCommand<AuthPlain>);
  static_assert(aurora::mail::common::ProtocolCommand<AuthLogin>);
  static_assert(aurora::mail::common::ProtocolCommand<AuthXOAuth2>);
}

TEST(SmtpCommandTest, CommandVariant_Serialize)
{
  Command cmd1 = Ehlo{ "localhost" };
  EXPECT_EQ(serialize(cmd1).value(), "EHLO localhost\r\n");

  Command cmd2 = MailFrom{ "test@example.com" };
  EXPECT_EQ(serialize(cmd2).value(), "MAIL FROM:<test@example.com>\r\n");

  Command cmd3 = Quit{};
  EXPECT_EQ(serialize(cmd3).value(), "QUIT\r\n");
}

TEST(SmtpCommandTest, AuthVariant_Serialize)
{
  AuthVariant auth1 = AuthPlain{ "user", "pass" };
  auto result1 = serialize(auth1);
  ASSERT_TRUE(result1.has_value());
  EXPECT_TRUE(result1->starts_with("AUTH PLAIN "));

  AuthVariant auth2 = AuthLogin{ "user", "pass" };
  EXPECT_EQ(serialize(auth2).value(), "AUTH LOGIN\r\n");
}

TEST(SmtpCommandTest, LongDomainName)
{
  Ehlo cmd{ "very.long.subdomain.example.corporation.com" };
  EXPECT_EQ(ok(cmd), "EHLO very.long.subdomain.example.corporation.com\r\n");
}

TEST(SmtpCommandTest, EmailWithPlusAddressing)
{
  MailFrom sender{ "user+tag+filter@example.com" };
  RcptTo recipient{ "admin+support@example.org" };

  EXPECT_EQ(ok(sender), "MAIL FROM:<user+tag+filter@example.com>\r\n");
  EXPECT_EQ(ok(recipient), "RCPT TO:<admin+support@example.org>\r\n");
}

TEST(SmtpCommandTest, UnicodeInCommands)
{
  // SMTP should handle UTF-8 byte sequences without rejecting (no CR/LF/NUL).
  Vrfy cmd{ "Müller" };
  auto result = ok(cmd);
  EXPECT_TRUE(result.starts_with("VRFY "));
  EXPECT_TRUE(result.ends_with("\r\n"));
}

TEST(SmtpCommandTest, AllCommandsHaveCRLF)
{
  EXPECT_TRUE(ok(Helo{ "test" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Ehlo{ "test" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(MailFrom{ "test@test.com" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(RcptTo{ "test@test.com" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Data{}).ends_with("\r\n"));
  EXPECT_TRUE(ok(Quit{}).ends_with("\r\n"));
  EXPECT_TRUE(ok(Rset{}).ends_with("\r\n"));
  EXPECT_TRUE(ok(StartTls{}).ends_with("\r\n"));
  EXPECT_TRUE(ok(Noop{ "" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Vrfy{ "test" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(Help{ "" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(AuthPlain{ "u", "p" }).ends_with("\r\n"));
  EXPECT_TRUE(ok(AuthLogin{ "u", "p" }).ends_with("\r\n"));
}
