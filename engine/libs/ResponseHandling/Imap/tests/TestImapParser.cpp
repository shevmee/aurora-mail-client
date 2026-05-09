#include <gtest/gtest.h>

#include <ImapParser.hpp>
#include <ImapResponse.hpp>

using aurora::mail::imap::response::ImapResponse;
using aurora::mail::imap::response::isGreetingLine;
using aurora::mail::imap::response::parse;
using aurora::mail::imap::response::StatusType;
using aurora::mail::imap::response::statusTypeToString;
using aurora::mail::imap::response::stringToStatusType;

// ---------------------------------------------------------------------------
// stringToStatusType / statusTypeToString
// ---------------------------------------------------------------------------

TEST(ImapStatusType, RoundTripKnownValues)
{
  for (auto s : { StatusType::OK, StatusType::NO, StatusType::BAD, StatusType::BYE, StatusType::PREAUTH })
  {
    EXPECT_EQ(stringToStatusType(statusTypeToString(s)), s);
  }
}

TEST(ImapStatusType, UnknownStringMapsToUndefined)
{
  EXPECT_EQ(stringToStatusType(""), StatusType::Undefined);
  EXPECT_EQ(stringToStatusType("DUNNO"), StatusType::Undefined);
}

TEST(ImapStatusType, ToStringIsUppercase)
{
  EXPECT_EQ(statusTypeToString(StatusType::OK), "OK");
  EXPECT_EQ(statusTypeToString(StatusType::BAD), "BAD");
  EXPECT_EQ(statusTypeToString(StatusType::PREAUTH), "PREAUTH");
}

TEST(ImapStatusType, LowercaseStringDoesNotMatch)
{
  // INCONSISTENCY DOC: stringToStatusType is case-sensitive. Real servers
  // always send uppercase status codes (RFC 3501 §7), so this is fine, but
  // any future caller that lowercases input must re-uppercase it before
  // looking up the status.
  EXPECT_EQ(stringToStatusType("ok"), StatusType::Undefined);
  EXPECT_EQ(stringToStatusType("Ok"), StatusType::Undefined);
}

// ---------------------------------------------------------------------------
// isGreetingLine
// ---------------------------------------------------------------------------

TEST(ImapGreeting, RecognisesOkGreeting)
{
  EXPECT_TRUE(isGreetingLine("* OK IMAP4rev1 Service Ready"));
}

TEST(ImapGreeting, RecognisesPreauthGreeting)
{
  EXPECT_TRUE(isGreetingLine("* PREAUTH already-authenticated"));
}

TEST(ImapGreeting, RecognisesByeGreeting)
{
  EXPECT_TRUE(isGreetingLine("* BYE refusing connection"));
}

TEST(ImapGreeting, RejectsTaggedResponses)
{
  EXPECT_FALSE(isGreetingLine("A001 OK Login completed"));
}

TEST(ImapGreeting, RejectsNonGreetingUntagged)
{
  EXPECT_FALSE(isGreetingLine("* 5 EXISTS"));
  EXPECT_FALSE(isGreetingLine("* CAPABILITY IMAP4rev1"));
}

TEST(ImapGreeting, IsCaseSensitive)
{
  // INCONSISTENCY DOC: starts_with() is exact-match on the prefix. RFC 3501
  // requires uppercase, so this matches reality, but it is worth pinning.
  EXPECT_FALSE(isGreetingLine("* ok"));
  EXPECT_FALSE(isGreetingLine("* Ok"));
}

// ---------------------------------------------------------------------------
// parse() - tagged responses
// ---------------------------------------------------------------------------

TEST(ImapParser, SimpleTaggedOk)
{
  auto r = parse("A001 OK LOGIN completed\r\n");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->tag, "A001");
  EXPECT_EQ(r->status, StatusType::OK);
  EXPECT_EQ(r->text, "LOGIN completed");
  EXPECT_TRUE(r->isSuccess());
  EXPECT_FALSE(r->hasUntagged());
}

TEST(ImapParser, TaggedNoIsFailure)
{
  auto r = parse("A002 NO Authentication failed\r\n");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->status, StatusType::NO);
  EXPECT_FALSE(r->isSuccess());
}

TEST(ImapParser, TaggedBadIsFailure)
{
  auto r = parse("A003 BAD command syntax error\r\n");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->status, StatusType::BAD);
  EXPECT_FALSE(r->isSuccess());
}

// ---------------------------------------------------------------------------
// parse() - untagged + tagged
// ---------------------------------------------------------------------------

TEST(ImapParser, UntaggedExistsThenTagged)
{
  // Regression: prior to the skipLineEndings() fix, the parser dropped every
  // untagged line after the first because skipSpaces only consumes ' '/'\t'.
  auto r = parse(
      "* 5 EXISTS\r\n"
      "* 2 RECENT\r\n"
      "A004 OK Done\r\n");
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->tag, "A004");
  EXPECT_EQ(r->status, StatusType::OK);
  ASSERT_EQ(r->untagged.size(), 2U);
  EXPECT_NE(r->untagged[0].line.find("EXISTS"), std::string_view::npos);
  EXPECT_NE(r->untagged[1].line.find("RECENT"), std::string_view::npos);
}

TEST(ImapParser, UntaggedNumericResponse_CommandFieldIsEmpty_INCONSISTENCY)
{
  // INCONSISTENCY: UntaggedResponse::command is documented as
  //   "Command type (FETCH, EXISTS, etc.)"
  // but the parser only sets it from getAtomValue() of the *second* token.
  // For the most common form of untagged responses --
  //   * 5 EXISTS
  //   * 12 FETCH (...)
  //   * 3 RECENT
  // -- the second token is a Number, getAtomValue returns nullopt, and the
  // command field is left as the empty string. The actual command name is
  // still present in parsed_values[1] as an Atom, so callers must walk the
  // values list rather than trusting `command`.
  //
  // We pin the broken behaviour so that any future fix has to update this
  // assertion (and so that CI surfaces the inconsistency to maintainers).
  auto r = parse("* 5 EXISTS\r\nA001 OK\r\n");
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->untagged.size(), 1U);
  EXPECT_TRUE(r->untagged[0].command.empty())
      << "Untagged-numeric command field unexpectedly populated to: '" << r->untagged[0].command
      << "'. If ImapParser was fixed to look ahead "
         "past the leading message number, replace this assertion with "
         "EXPECT_EQ(... .command, \"EXISTS\").";
  // Sanity: the EXISTS token *is* visible if you walk parsed_values.
  ASSERT_GE(r->untagged[0].parsed_values.size(), 2U);
}

TEST(ImapParser, UntaggedAtomCommandPopulatesCommandField)
{
  // For non-numeric untagged responses (CAPABILITY, LIST, OK, etc.) the
  // current parser *does* populate `command` correctly, since the second
  // token is an atom.
  auto r = parse(
      "* CAPABILITY IMAP4rev1 STARTTLS\r\n"
      "A001 OK Done\r\n");
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->untagged.size(), 1U);
  EXPECT_EQ(r->untagged[0].command, "CAPABILITY");
}

TEST(ImapParser, UntaggedOnlyGetsSyntheticOkTag)
{
  auto r = parse("* 5 EXISTS\r\n");
  ASSERT_TRUE(r.has_value());
  // Without a tagged line, parse() synthesises a tag = "*" and status = OK
  // so the response still routes through the success path. We pin this so a
  // future "strict" rewrite can flag the implicit promotion.
  EXPECT_EQ(r->tag, "*");
  EXPECT_EQ(r->status, StatusType::OK);
  EXPECT_TRUE(r->hasUntagged());
}

TEST(ImapParser, UntaggedListContainsParsedValues)
{
  auto r = parse(
      "* LIST () \"/\" \"INBOX\"\r\n"
      "A005 OK LIST completed\r\n");
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->untagged.size(), 1U);
  EXPECT_EQ(r->untagged[0].command, "LIST");
  // command + 3 values: list, "/", "INBOX"
  EXPECT_GE(r->untagged[0].parsed_values.size(), 4U);
}

// ---------------------------------------------------------------------------
// parse() - greetings
// ---------------------------------------------------------------------------

TEST(ImapParser, GreetingOkAccepted)
{
  auto r = parse("* OK IMAP4rev1 Service Ready\r\n", /*is_greeting=*/true);
  ASSERT_TRUE(r.has_value()) << r.error();
  EXPECT_EQ(r->tag, "*");
  EXPECT_EQ(r->status, StatusType::OK);
  EXPECT_NE(r->text.find("Service Ready"), std::string::npos);
}

TEST(ImapParser, GreetingPreauthAccepted)
{
  auto r = parse("* PREAUTH already authenticated\r\n", true);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->status, StatusType::PREAUTH);
}

TEST(ImapParser, GreetingByeAccepted)
{
  auto r = parse("* BYE Server closing\r\n", true);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->status, StatusType::BYE);
}

TEST(ImapParser, GreetingNoIsRejected)
{
  // is_greeting=true demands OK / PREAUTH / BYE.
  auto r = parse("* NO Service unavailable\r\n", true);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().find("Invalid IMAP greeting status"), std::string::npos);
}

TEST(ImapParser, GreetingEmptyInputRejected)
{
  auto r = parse("", true);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), "Invalid IMAP greeting format");
}

// ---------------------------------------------------------------------------
// Continuation request '+'
// ---------------------------------------------------------------------------

TEST(ImapParser, ContinuationRequestParsedAsUntagged)
{
  // AUTHENTICATE flow: server responds with "+\r\n" or "+ <base64>\r\n" to
  // request the next chunk of credentials.
  auto r = parse("+ Ready for client data\r\n");
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->untagged.size(), 1U);
  EXPECT_EQ(r->untagged[0].command, "Ready");
}

// ---------------------------------------------------------------------------
// Empty input
// ---------------------------------------------------------------------------

TEST(ImapParser, EmptyInputProducesEmptyResponse)
{
  // INCONSISTENCY DOC: an empty buffer is currently treated as success with
  // a default-constructed ImapResponse (no tag, status=Undefined, no
  // untagged). Callers that expect parse() to fail on empty input will be
  // surprised. The is_greeting path *does* fail; the regular path does not.
  auto r = parse("");
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->tag.empty());
  EXPECT_EQ(r->status, StatusType::Undefined);
  EXPECT_FALSE(r->hasUntagged());
}
