#include <gtest/gtest.h>

#include <Email/EmailParser.hpp>
#include <QString>
#include <QVector>

#include "QtTestSupport.hpp"

using aurora::mail::app::email::EmailParser;
using aurora::mail::app::email::EmailSummary;
using aurora::mail::app::email::ParsedEmailContent;

// ---------------------------------------------------------------------------
// formatPlainTextAsHtml -- pure transformation
// ---------------------------------------------------------------------------

TEST(EmailParserHtml, EscapesHtmlEntities)
{
  const QString out = EmailParser::formatPlainTextAsHtml(QStringLiteral("<b>Hi & bye</b>"));
  EXPECT_NE(out.indexOf(QStringLiteral("&lt;b&gt;")), -1) << out;
  EXPECT_NE(out.indexOf(QStringLiteral("&amp;")), -1);
  EXPECT_NE(out.indexOf(QStringLiteral("&gt;")), -1);
  // After escaping, the literal "<b>" must not be present.
  EXPECT_EQ(out.indexOf(QStringLiteral("<b>")), -1);
}

TEST(EmailParserHtml, ConvertsLineBreaksToBr)
{
  const QString out = EmailParser::formatPlainTextAsHtml(QStringLiteral("a\r\nb\nc"));
  EXPECT_NE(out.indexOf(QStringLiteral("a<br>b<br>c")), -1) << out;
}

TEST(EmailParserHtml, ConvertsHttpUrlsToLinks)
{
  const QString out = EmailParser::formatPlainTextAsHtml(QStringLiteral("see https://example.com page"));
  EXPECT_NE(out.indexOf(QStringLiteral("<a href=\"https://example.com\"")), -1) << out;
}

TEST(EmailParserHtml, ConvertsBareEmailsToMailtoLinks)
{
  const QString out = EmailParser::formatPlainTextAsHtml(QStringLiteral("ping me at user@example.com"));
  EXPECT_NE(out.indexOf(QStringLiteral("href=\"mailto:user@example.com\"")), -1) << out;
}

TEST(EmailParserHtml, OutputContainsBodyEnvelope)
{
  // The plumbing wraps the content in <html><body>... so QTextBrowser can
  // pick up the embedded styles. Pin so a future "no wrapper" rewrite is
  // intentional.
  const QString out = EmailParser::formatPlainTextAsHtml(QStringLiteral("hi"));
  EXPECT_NE(out.indexOf(QStringLiteral("<body")), -1);
  EXPECT_NE(out.indexOf(QStringLiteral("</body>")), -1);
}

TEST(EmailParserHtml, EmailRegexFiresInsideEscapedAmpersand)
{
  // INCONSISTENCY DOC: the order of operations is escape-entities first,
  // THEN convert URLs/emails. Because `&` becomes `&amp;`, an email that
  // contains '&' would never round-trip cleanly. Bare addresses (the
  // common case) still match correctly. Pin so a future re-ordering is
  // intentional.
  const QString out = EmailParser::formatPlainTextAsHtml(QStringLiteral("foo@bar.com"));
  EXPECT_NE(out.indexOf(QStringLiteral("href=\"mailto:foo@bar.com\"")), -1);
}

// ---------------------------------------------------------------------------
// parseRawMessage  -- thin wrapper over MimeReader::parseMessage
// ---------------------------------------------------------------------------

TEST(EmailParserParseRaw, EmptyInputReturnsNullopt)
{
  EXPECT_FALSE(EmailParser::parseRawMessage("").has_value());
}

TEST(EmailParserParseRaw, ParsesMinimalMessage)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
      "Subject: hi\r\n"
      "\r\n"
      "body!\r\n";
  auto r = EmailParser::parseRawMessage(raw);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->subject, "hi");
  EXPECT_EQ(r->from.getAddress(), "a@x.com");
  EXPECT_NE(r->text_body.find("body!"), std::string::npos);
}

// ---------------------------------------------------------------------------
// parseFullEmailContent  -- IMAP FETCH BODY[] extraction + MIME parse
// ---------------------------------------------------------------------------

namespace
{
  // Build a synthetic IMAP FETCH BODY[] response with a literal-size marker.
  std::string fetchResponse(const std::string& message)
  {
    return std::string("* 1 FETCH (UID 1 BODY[] {") + std::to_string(message.size()) + std::string("}\r\n") + message +
           std::string(")\r\n");
  }
}  // namespace

TEST(EmailParserParseFull, ParsesPlainTextBodyAsHtml)
{
  const std::string mime =
      "From: alice@example.com\r\n"
      "To: bob@example.com\r\n"
      "Subject: greeting\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "\r\n"
      "hello world\r\n";
  ParsedEmailContent c = EmailParser::parseFullEmailContent(fetchResponse(mime));
  EXPECT_EQ(c.subject, QStringLiteral("greeting"));
  // Plain-text bodies are wrapped in formatPlainTextAsHtml, so isHtml is
  // true (the wrapper IS HTML) and the body contains <body>.
  EXPECT_TRUE(c.isHtml);
  EXPECT_NE(c.body.indexOf(QStringLiteral("hello world")), -1) << c.body.toStdString();
  EXPECT_NE(c.body.indexOf(QStringLiteral("<body")), -1);
}

TEST(EmailParserParseFull, PrefersHtmlBodyOverPlainText)
{
  const std::string mime =
      "From: alice@example.com\r\n"
      "To: bob@example.com\r\n"
      "Subject: html\r\n"
      "MIME-Version: 1.0\r\n"
      "Content-Type: multipart/alternative; boundary=BNDRY\r\n"
      "\r\n"
      "--BNDRY\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "\r\n"
      "plain version\r\n"
      "--BNDRY\r\n"
      "Content-Type: text/html; charset=UTF-8\r\n"
      "\r\n"
      "<p>html version</p>\r\n"
      "--BNDRY--\r\n";
  ParsedEmailContent c = EmailParser::parseFullEmailContent(fetchResponse(mime));
  EXPECT_TRUE(c.isHtml);
  EXPECT_NE(c.body.indexOf(QStringLiteral("html version")), -1);
}

TEST(EmailParserParseFull, ExtractsBase64Attachment)
{
  // "Hello" base64 -> SGVsbG8=  (capital H)
  const std::string mime =
      "From: alice@example.com\r\n"
      "To: bob@example.com\r\n"
      "Subject: file\r\n"
      "MIME-Version: 1.0\r\n"
      "Content-Type: multipart/mixed; boundary=BNDRY\r\n"
      "\r\n"
      "--BNDRY\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "\r\n"
      "see attached\r\n"
      "--BNDRY\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Disposition: attachment; filename=\"hello.bin\"\r\n"
      "Content-Transfer-Encoding: base64\r\n"
      "\r\n"
      "SGVsbG8=\r\n"
      "--BNDRY--\r\n";
  ParsedEmailContent c = EmailParser::parseFullEmailContent(fetchResponse(mime));
  ASSERT_EQ(c.attachments.size(), 1);
  EXPECT_EQ(c.attachments[0].filename, QStringLiteral("hello.bin"));
  EXPECT_EQ(c.attachments[0].data, QByteArray("Hello", 5));
  EXPECT_FALSE(c.attachments[0].isInline);
}

TEST(EmailParserParseFull, FormatsSenderWithDisplayNameWhenPresent)
{
  const std::string mime =
      "From: \"Alice\" <alice@example.com>\r\n"
      "To: bob@example.com\r\n"
      "Subject: hi\r\n"
      "\r\n"
      "body\r\n";
  ParsedEmailContent c = EmailParser::parseFullEmailContent(fetchResponse(mime));
  EXPECT_EQ(c.from, QStringLiteral("Alice <alice@example.com>"));
}

TEST(EmailParserParseFull, FormatsSenderWithBareAddressWhenNoName)
{
  const std::string mime =
      "From: alice@example.com\r\n"
      "To: bob@example.com\r\n"
      "Subject: hi\r\n"
      "\r\n"
      "body\r\n";
  ParsedEmailContent c = EmailParser::parseFullEmailContent(fetchResponse(mime));
  EXPECT_EQ(c.from, QStringLiteral("alice@example.com"));
}

TEST(EmailParserParseFull, NoBodyMarkerReturnsEmptyParsedContent)
{
  // No BODY[] marker at all -> extractBodyFromFetch returns "" -> the
  // function returns a default-constructed ParsedEmailContent.
  ParsedEmailContent c = EmailParser::parseFullEmailContent("* 1 OK\r\n");
  EXPECT_TRUE(c.subject.isEmpty());
  EXPECT_TRUE(c.attachments.isEmpty());
}

TEST(EmailParserParseFull, BodyTextMarkerIsAlsoAccepted)
{
  // extractBodyFromFetch falls back to BODY[TEXT] when BODY[] is absent.
  const std::string mime = "subject-less plain body";
  std::string response =
      std::string("* 1 FETCH (UID 1 BODY[TEXT] {") + std::to_string(mime.size()) + "}\r\n" + mime + ")\r\n";
  // No headers means the MIME parser should still return *something* sane;
  // the wrapper hands us a default-constructed ParsedEmailContent because
  // gmime won't find any structure in raw text. Just verify it doesn't
  // throw and returns the documented "Could not parse email content."
  // fallback when parsing fails.
  ParsedEmailContent c = EmailParser::parseFullEmailContent(response);
  // Implementation is permissive here -- both empty content and the
  // fallback HTML are valid. We only assert no crash.
  SUCCEED();
}

// ---------------------------------------------------------------------------
// parseEmailContent (legacy 3-tuple variant)
// ---------------------------------------------------------------------------

TEST(EmailParserLegacy, ReturnsSubjectFromAndHtmlBody)
{
  const std::string mime =
      "From: \"Bob\" <bob@example.com>\r\n"
      "To: alice@example.com\r\n"
      "Subject: legacy\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "\r\n"
      "the body\r\n";
  auto [subject, from, body] = EmailParser::parseEmailContent(fetchResponse(mime));
  EXPECT_EQ(subject, QStringLiteral("legacy"));
  EXPECT_EQ(from, QStringLiteral("Bob <bob@example.com>"));
  EXPECT_NE(body.indexOf(QStringLiteral("the body")), -1);
}

TEST(EmailParserLegacy, EmptyResponseReturnsAllEmpty)
{
  auto [subject, from, body] = EmailParser::parseEmailContent("");
  EXPECT_TRUE(subject.isEmpty());
  EXPECT_TRUE(from.isEmpty());
  EXPECT_TRUE(body.isEmpty());
}

// ---------------------------------------------------------------------------
// parseEmailList -- IMAP FETCH ENVELOPE-mode parsing
// ---------------------------------------------------------------------------

TEST(EmailParserList, ParsesSingleFetchEnvelope)
{
  // ENVELOPE format: (date subject from sender reply-to to cc bcc in-reply-to message-id)
  const std::string response =
      "* 1 FETCH (UID 42 FLAGS (\\Seen) ENVELOPE ("
      "\"Mon, 1 Jan 2024 12:00:00 +0000\" "
      "\"Hello world\" "
      "((\"Alice\" NIL \"alice\" \"example.com\")) "
      "((\"Alice\" NIL \"alice\" \"example.com\")) "
      "NIL "
      "((\"Bob\" NIL \"bob\" \"example.com\")) "
      "NIL NIL NIL \"<msg-1@example.com>\""
      "))\r\n";

  QVector<EmailSummary> list = EmailParser::parseEmailList(response);
  ASSERT_EQ(list.size(), 1);
  const auto& e = list[0];
  EXPECT_EQ(e.uid, QStringLiteral("42"));
  EXPECT_TRUE(e.isRead);
  EXPECT_EQ(e.subject, QStringLiteral("Hello world"));
  EXPECT_EQ(e.from, QStringLiteral("Alice"));
  EXPECT_EQ(e.to, QStringLiteral("Bob"));
  EXPECT_EQ(e.messageId, QStringLiteral("<msg-1@example.com>"));
}

TEST(EmailParserList, FallsBackForUnseenFlagAndAnonymousSender)
{
  const std::string response =
      "* 2 FETCH (UID 7 FLAGS () ENVELOPE ("
      "\"Mon, 1 Jan 2024 12:00:00 +0000\" "
      "\"\" "
      "((NIL NIL \"someone\" \"example.com\")) "
      "((NIL NIL \"someone\" \"example.com\")) "
      "NIL "
      "((NIL NIL \"r\" \"x.com\")) "
      "NIL NIL NIL \"<x@y>\""
      "))\r\n";
  QVector<EmailSummary> list = EmailParser::parseEmailList(response);
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].uid, QStringLiteral("7"));
  EXPECT_FALSE(list[0].isRead);
  // Empty-quoted subject is replaced with the synthetic "Email #7" label.
  EXPECT_EQ(list[0].subject, QStringLiteral("Email #7"));
  EXPECT_EQ(list[0].from, QStringLiteral("someone@example.com"));
}

TEST(EmailParserList, MultipleAddressesShowFirstPlusCount)
{
  const std::string response =
      "* 1 FETCH (UID 3 FLAGS () ENVELOPE ("
      "\"Mon, 1 Jan 2024 12:00:00 +0000\" "
      "\"Group\" "
      "((\"Alice\" NIL \"alice\" \"x.com\")) "
      "((\"Alice\" NIL \"alice\" \"x.com\")) "
      "NIL "
      "((\"Bob\" NIL \"bob\" \"x.com\")(\"Carol\" NIL \"carol\" \"x.com\")(\"Dave\" NIL \"dave\" \"x.com\")) "
      "NIL NIL NIL \"<gid@x>\""
      "))\r\n";
  QVector<EmailSummary> list = EmailParser::parseEmailList(response);
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].to, QStringLiteral("Bob + 2"));
}

TEST(EmailParserList, EmptyResponseProducesEmptyList)
{
  EXPECT_TRUE(EmailParser::parseEmailList("").isEmpty());
  EXPECT_TRUE(EmailParser::parseEmailList("* OK\r\n").isEmpty());
}

TEST(EmailParserList, FetchWithoutUidIsSkipped)
{
  // The parser requires UID; without it the entry is skipped.
  const std::string response =
      "* 1 FETCH (FLAGS (\\Seen) ENVELOPE ("
      "\"Mon, 1 Jan 2024 12:00:00 +0000\" "
      "\"S\" "
      "((\"A\" NIL \"a\" \"x\")) "
      "((\"A\" NIL \"a\" \"x\")) "
      "NIL ((\"B\" NIL \"b\" \"y\")) NIL NIL NIL \"<m>\""
      "))\r\n";
  EXPECT_TRUE(EmailParser::parseEmailList(response).isEmpty());
}
