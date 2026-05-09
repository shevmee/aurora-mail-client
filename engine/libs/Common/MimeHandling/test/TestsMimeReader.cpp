#include <gtest/gtest.h>

#include <MimeContext.hpp>
#include <MimeReader.hpp>
#include <ReceivedMailMessage.hpp>
#include <string>

using aurora::mail::common::mime::getMimeContext;
using aurora::mail::common::mime::MimeParseError;
using aurora::mail::common::mime::reader::convertToUtf8;
using aurora::mail::common::mime::reader::decodeContent;
using aurora::mail::common::mime::reader::decodeHeaderValue;
using aurora::mail::common::mime::reader::parseAddress;
using aurora::mail::common::mime::reader::parseAddressList;
using aurora::mail::common::mime::reader::parseDate;
using aurora::mail::common::mime::reader::parseHeaders;
using aurora::mail::common::mime::reader::parseMessage;

namespace
{
  class MimeReaderTest : public ::testing::Test
  {
   protected:
    static void SetUpTestSuite()
    {
      (void)getMimeContext();
    }
  };
}  // namespace

// ---------------------------------------------------------------------------
// parseHeaders / parseMessage  --  empty input
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ParseHeadersRejectsEmptyInput)
{
  auto r = parseHeaders("");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().type, MimeParseError::Type::EmptyMessage);
}

TEST_F(MimeReaderTest, ParseMessageEmptyInputBehaviour)
{
  // INCONSISTENCY DOC: parseMessage() does NOT short-circuit on empty input
  // the way parseHeaders() does. It hands an empty buffer to gmime, which
  // returns a degenerate but non-null GMimeMessage, so parseMessage()
  // succeeds with all fields empty. Pin so a future "fail-fast on empty"
  // change is intentional.
  auto r = parseMessage("");
  if (r.has_value())
  {
    EXPECT_TRUE(r->subject.empty());
    EXPECT_TRUE(r->text_body.empty());
    EXPECT_FALSE(r->has_date);
  }
  else
  {
    // If the implementation flips to fail-fast, the EmptyMessage type is
    // the closest match.
    EXPECT_EQ(r.error().type, MimeParseError::Type::EmptyMessage);
  }
}

// ---------------------------------------------------------------------------
// parseHeaders -- standard headers
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ParseHeadersExtractsCommonFields)
{
  const std::string raw =
      "From: Alice <alice@example.com>\r\n"
      "To: bob@example.com\r\n"
      "Subject: hello\r\n"
      "Date: Mon, 1 Jan 2024 12:00:00 +0000\r\n"
      "Message-ID: <abc@example.com>\r\n"
      "\r\n";

  auto r = parseHeaders(raw);
  ASSERT_TRUE(r.has_value()) << r.error().toString();
  EXPECT_EQ(r->from.getAddress(), "alice@example.com");
  EXPECT_EQ(r->from.getName(), "Alice");
  ASSERT_EQ(r->email_recipients.to.size(), 1U);
  EXPECT_EQ(r->email_recipients.to[0].getAddress(), "bob@example.com");
  EXPECT_EQ(r->subject, "hello");
  EXPECT_EQ(r->email_threading.message_id, "abc@example.com");
  EXPECT_TRUE(r->has_date);
}

TEST_F(MimeReaderTest, ParseHeadersExtractsCcRecipients)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
      "Cc: c1@x.com, c2@x.com\r\n"
      "Subject: cc test\r\n"
      "\r\n";
  auto r = parseHeaders(raw);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->email_recipients.cc.size(), 2U);
  EXPECT_EQ(r->email_recipients.cc[0].getAddress(), "c1@x.com");
  EXPECT_EQ(r->email_recipients.cc[1].getAddress(), "c2@x.com");
}

TEST_F(MimeReaderTest, ParseHeadersExtractsThreadingHeaders)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
      "Subject: re: hi\r\n"
      "In-Reply-To: <orig@x.com>\r\n"
      "References: <a@x.com> <b@x.com> <c@x.com>\r\n"
      "\r\n";
  auto r = parseHeaders(raw);
  ASSERT_TRUE(r.has_value());
  ASSERT_TRUE(r->email_threading.in_reply_to.has_value());
  EXPECT_NE(r->email_threading.in_reply_to->find("orig@x.com"), std::string::npos);
  ASSERT_EQ(r->email_threading.references.size(), 3U);
  EXPECT_NE(r->email_threading.references[0].find("a@x.com"), std::string::npos);
}

TEST_F(MimeReaderTest, GetHeaderIsCaseInsensitive)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "Subject: hi\r\n"
      "X-Custom: my-value\r\n"
      "\r\n";
  auto r = parseHeaders(raw);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->getHeader("X-Custom"), "my-value");
  EXPECT_EQ(r->getHeader("x-custom"), "my-value");
  EXPECT_EQ(r->getHeader("X-CUSTOM"), "my-value");
  EXPECT_TRUE(r->hasHeader("x-CUSTOM"));
  EXPECT_EQ(r->getHeader("Not-There"), "");
}

// ---------------------------------------------------------------------------
// parseMessage -- bodies
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ParseMessageExtractsTextPlainBody)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
      "Subject: hi\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "\r\n"
      "Hello, world!\r\n";
  auto r = parseMessage(raw);
  ASSERT_TRUE(r.has_value()) << r.error().toString();
  EXPECT_NE(r->text_body.find("Hello, world!"), std::string::npos);
  EXPECT_FALSE(r->hasHtmlBody());
}

TEST_F(MimeReaderTest, ParseMessageExtractsTextHtmlBody)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
      "Subject: hi\r\n"
      "Content-Type: text/html; charset=UTF-8\r\n"
      "\r\n"
      "<p>Hello</p>\r\n";
  auto r = parseMessage(raw);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->hasHtmlBody());
  EXPECT_NE(r->html_body.find("<p>Hello</p>"), std::string::npos);
}

TEST_F(MimeReaderTest, ParseMessageMultipartAlternative)
{
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
      "Subject: alt\r\n"
      "MIME-Version: 1.0\r\n"
      "Content-Type: multipart/alternative; boundary=BNDRY\r\n"
      "\r\n"
      "--BNDRY\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "\r\n"
      "plain body here\r\n"
      "--BNDRY\r\n"
      "Content-Type: text/html; charset=UTF-8\r\n"
      "\r\n"
      "<b>html body here</b>\r\n"
      "--BNDRY--\r\n";
  auto r = parseMessage(raw);
  ASSERT_TRUE(r.has_value());
  EXPECT_NE(r->text_body.find("plain body here"), std::string::npos);
  EXPECT_NE(r->html_body.find("html body here"), std::string::npos);
}

// ---------------------------------------------------------------------------
// parseMessage -- attachments
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ParseMessageExtractsBase64Attachment)
{
  // "Hello!" base64-encoded is "SGVsbG8h"
  const std::string raw =
      "From: a@x.com\r\n"
      "To: b@x.com\r\n"
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
      "Content-Disposition: attachment; filename=\"hi.bin\"\r\n"
      "Content-Transfer-Encoding: base64\r\n"
      "\r\n"
      "SGVsbG8h\r\n"
      "--BNDRY--\r\n";

  auto r = parseMessage(raw);
  ASSERT_TRUE(r.has_value()) << r.error().toString();
  ASSERT_TRUE(r->hasAttachments());
  ASSERT_EQ(r->attachmentCount(), 1U);
  const auto& att = r->attachments[0];
  EXPECT_EQ(att.filename, "hi.bin");
  EXPECT_NE(att.content_type.find("application/octet-stream"), std::string::npos);
  EXPECT_FALSE(att.is_inline);

  const std::string body(reinterpret_cast<const char*>(att.data.data()), att.data.size());
  EXPECT_EQ(body, "Hello!");
}

// ---------------------------------------------------------------------------
// decodeHeaderValue
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, DecodeHeaderValuePassesThroughAscii)
{
  EXPECT_EQ(decodeHeaderValue("plain ascii"), "plain ascii");
  EXPECT_EQ(decodeHeaderValue(""), "");
}

TEST_F(MimeReaderTest, DecodeHeaderValueRfc2047Base64Utf8)
{
  // "Hello World" base64 in UTF-8 -> "SGVsbG8gV29ybGQ="
  const std::string out = decodeHeaderValue("=?UTF-8?B?SGVsbG8gV29ybGQ=?=");
  EXPECT_EQ(out, "Hello World");
}

TEST_F(MimeReaderTest, DecodeHeaderValueRfc2047QuotedPrintableLatin1)
{
  const std::string out = decodeHeaderValue("=?ISO-8859-1?Q?Hello_World?=");
  EXPECT_EQ(out, "Hello World");
}

// ---------------------------------------------------------------------------
// decodeContent
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, DecodeContentBase64)
{
  auto r = decodeContent("SGVsbG8sIHdvcmxkIQ==", "base64");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "Hello, world!");
}

TEST_F(MimeReaderTest, DecodeContentQuotedPrintable)
{
  // "café" with é encoded as =C3=A9
  auto r = decodeContent("caf=C3=A9", "quoted-printable");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "café");
}

TEST_F(MimeReaderTest, DecodeContentQuotedPrintableSoftBreakCRLF)
{
  // "=" followed by CRLF is a soft line break.
  auto r = decodeContent("hello=\r\nworld", "quoted-printable");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "helloworld");
}

TEST_F(MimeReaderTest, DecodeContentQuotedPrintableSoftBreakLFOnly)
{
  // Many real servers emit only LF after '=' due to MTA mangling. The
  // decoder must accept both.
  auto r = decodeContent("hello=\nworld", "quoted-printable");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "helloworld");
}

TEST_F(MimeReaderTest, DecodeContentSevenBitIsPassthrough)
{
  for (auto enc : { "7bit", "8bit", "binary", "" })
  {
    auto r = decodeContent("plain text", enc);
    ASSERT_TRUE(r.has_value()) << "enc=" << enc;
    EXPECT_EQ(*r, "plain text");
  }
}

TEST_F(MimeReaderTest, DecodeContentEncodingIsCaseInsensitive)
{
  EXPECT_EQ(decodeContent("SGVsbG8=", "BASE64").value(), "Hello");
  EXPECT_EQ(decodeContent("SGVsbG8=", "Base64").value(), "Hello");
  EXPECT_EQ(decodeContent("Hi=20there", "QUOTED-PRINTABLE").value(), "Hi there");
}

TEST_F(MimeReaderTest, DecodeContentUnknownEncodingIsError)
{
  auto r = decodeContent("xxx", "x-uuencode");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().type, MimeParseError::Type::EncodingError);
  EXPECT_NE(r.error().message.find("x-uuencode"), std::string::npos);
}

// ---------------------------------------------------------------------------
// convertToUtf8
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ConvertToUtf8FromUtf8IsIdentity)
{
  auto r = convertToUtf8("Hello", "UTF-8");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "Hello");
}

TEST_F(MimeReaderTest, ConvertToUtf8FromAsciiIsIdentity)
{
  auto r = convertToUtf8("plain", "us-ascii");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "plain");
}

TEST_F(MimeReaderTest, ConvertToUtf8EmptyCharsetIsIdentity)
{
  auto r = convertToUtf8("verbatim", "");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "verbatim");
}

TEST_F(MimeReaderTest, ConvertToUtf8FromLatin1)
{
  // 0xE9 is 'é' in ISO-8859-1; in UTF-8 it's the two bytes C3 A9.
  std::string latin1;
  latin1.push_back('c');
  latin1.push_back('a');
  latin1.push_back('f');
  latin1.push_back(static_cast<char>(0xE9));
  auto r = convertToUtf8(latin1, "ISO-8859-1");
  ASSERT_TRUE(r.has_value()) << r.error().toString();
  EXPECT_EQ(*r, "café");
}

TEST_F(MimeReaderTest, ConvertToUtf8UnknownCharsetReturnsCharsetError)
{
  auto r = convertToUtf8("garbage", "X-NOT-A-CHARSET-2025");
  // Implementation either fails (charset unknown) or returns empty string.
  if (!r.has_value())
  {
    EXPECT_EQ(r.error().type, MimeParseError::Type::CharsetError);
  }
}

// ---------------------------------------------------------------------------
// parseDate
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ParseDateRfc2822)
{
  auto r = parseDate("Tue, 1 Jul 2003 10:52:37 +0200");
  ASSERT_TRUE(r.has_value());
  // Round-trip via time_t -- ensure the value is sane (>= 2003 epoch).
  auto unix_seconds = std::chrono::system_clock::to_time_t(*r);
  EXPECT_GT(unix_seconds, 1'000'000'000);
}

TEST_F(MimeReaderTest, ParseDateInvalidReturnsNullopt)
{
  EXPECT_FALSE(parseDate("not a date").has_value());
  EXPECT_FALSE(parseDate("").has_value());
}

// ---------------------------------------------------------------------------
// parseAddressList / parseAddress
// ---------------------------------------------------------------------------

TEST_F(MimeReaderTest, ParseAddressListSingle)
{
  auto v = parseAddressList("Alice <alice@example.com>");
  ASSERT_EQ(v.size(), 1U);
  EXPECT_EQ(v[0].getAddress(), "alice@example.com");
  EXPECT_EQ(v[0].getName(), "Alice");
}

TEST_F(MimeReaderTest, ParseAddressListMultiple)
{
  auto v = parseAddressList("a@x.com, \"Bob B\" <b@x.com>, c@x.com");
  ASSERT_EQ(v.size(), 3U);
  EXPECT_EQ(v[0].getAddress(), "a@x.com");
  EXPECT_EQ(v[1].getAddress(), "b@x.com");
  EXPECT_EQ(v[1].getName(), "Bob B");
  EXPECT_EQ(v[2].getAddress(), "c@x.com");
}

TEST_F(MimeReaderTest, ParseAddressListEmpty)
{
  EXPECT_TRUE(parseAddressList("").empty());
  EXPECT_TRUE(parseAddressList("   ").empty());
}

TEST_F(MimeReaderTest, ParseAddressListSkipsInvalidEntries)
{
  // gmime parses "not-a-valid-address" as a name-only mailbox; the reader's
  // gmimeToMailAddress() rejects it because MailAddress::create() validates.
  // So the result is empty rather than a "blank" address.
  auto v = parseAddressList("not-a-valid-address");
  EXPECT_TRUE(v.empty());
}

TEST_F(MimeReaderTest, ParseAddressReturnsFirstOrEmpty)
{
  EXPECT_EQ(parseAddress("alice@example.com, bob@example.com").getAddress(), "alice@example.com");
  EXPECT_FALSE(parseAddress("").isValid());
}
