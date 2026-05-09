#include <gtest/gtest.h>

#include <MailAddress.hpp>
#include <MailAttachment.hpp>
#include <MailMessage.hpp>
#include <MimeContext.hpp>
#include <MimeWriter.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using aurora::mail::common::mail::MailAddress;
using aurora::mail::common::mail::MailAttachment;
using aurora::mail::common::mail::MailMessage;
using aurora::mail::common::mime::getMimeContext;
using aurora::mail::common::mime::writer::buildMimeMessage;

namespace fs = std::filesystem;

namespace
{
  // GMime is not threadsafe and must be initialised exactly once per process;
  // touching getMimeContext() in SetUpTestSuite ensures the static MimeContext
  // is constructed before any test runs.
  class MimeWriterTest : public ::testing::Test
  {
   protected:
    static void SetUpTestSuite()
    {
      (void)getMimeContext();
    }

    MailMessage minimalMessage() const
    {
      MailMessage m;
      m.from = MailAddress("alice@example.com", "Alice");
      m.email_recipients.to.push_back(MailAddress("bob@example.com", "Bob"));
      m.subject = "Test subject";
      m.text_body = "Hello, Bob!";
      m.sender_domain = "example.com";
      return m;
    }
  };
}  // namespace

// ---------------------------------------------------------------------------
// Basic header generation
// ---------------------------------------------------------------------------

TEST_F(MimeWriterTest, WritesAllStandardHeaders)
{
  const std::string mime = buildMimeMessage(minimalMessage());

  EXPECT_NE(mime.find("From: "), std::string::npos);
  EXPECT_NE(mime.find("To: "), std::string::npos);
  EXPECT_NE(mime.find("Subject: "), std::string::npos);
  EXPECT_NE(mime.find("Date: "), std::string::npos);
  EXPECT_NE(mime.find("Message-Id:"), std::string::npos);
  EXPECT_NE(mime.find("alice@example.com"), std::string::npos);
  EXPECT_NE(mime.find("bob@example.com"), std::string::npos);
  EXPECT_NE(mime.find("Test subject"), std::string::npos);
  EXPECT_NE(mime.find("Hello, Bob!"), std::string::npos);
}

TEST_F(MimeWriterTest, UsesCrlfLineEndings)
{
  // RFC 5322 §2.1: CRLF is the canonical line terminator. The writer is
  // configured with GMIME_NEWLINE_FORMAT_DOS so output must be CRLF.
  const std::string mime = buildMimeMessage(minimalMessage());
  EXPECT_NE(mime.find("\r\n"), std::string::npos);
  // Spot-check: the From: line should end with CRLF.
  auto from_pos = mime.find("From: ");
  ASSERT_NE(from_pos, std::string::npos);
  auto eol = mime.find('\n', from_pos);
  ASSERT_NE(eol, std::string::npos);
  EXPECT_GT(eol, 0U);
  EXPECT_EQ(mime[eol - 1], '\r') << "From: line not terminated by CRLF";
}

TEST_F(MimeWriterTest, IncludesMimeVersion)
{
  // GMime adds MIME-Version: 1.0 automatically when constructing a message.
  const std::string mime = buildMimeMessage(minimalMessage());
  EXPECT_NE(mime.find("MIME-Version:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Recipient handling (To/CC/BCC)
// ---------------------------------------------------------------------------

TEST_F(MimeWriterTest, MultipleToAddresses)
{
  MailMessage m = minimalMessage();
  m.email_recipients.to.push_back(MailAddress("carol@example.com"));
  const std::string mime = buildMimeMessage(m);
  EXPECT_NE(mime.find("bob@example.com"), std::string::npos);
  EXPECT_NE(mime.find("carol@example.com"), std::string::npos);
}

TEST_F(MimeWriterTest, CcAddressesAppearInOutput)
{
  MailMessage m = minimalMessage();
  m.email_recipients.cc.push_back(MailAddress("dave@example.com"));
  const std::string mime = buildMimeMessage(m);
  EXPECT_NE(mime.find("Cc: "), std::string::npos);
  EXPECT_NE(mime.find("dave@example.com"), std::string::npos);
}

TEST_F(MimeWriterTest, BccHiddenForSmtpByDefault)
{
  MailMessage m = minimalMessage();
  m.email_recipients.bcc.push_back(MailAddress("eve@example.com"));
  const std::string mime = buildMimeMessage(m);  // hide_bcc=true default
  // The BCC header itself must not be in the wire form for SMTP -- but the
  // GMime "hidden header" mechanism still strips it from the output.
  EXPECT_EQ(mime.find("Bcc:"), std::string::npos) << "BCC header leaked into SMTP wire form: " << mime;
  EXPECT_EQ(mime.find("eve@example.com"), std::string::npos) << "BCC recipient leaked into SMTP wire form";
}

TEST_F(MimeWriterTest, BccVisibleWhenHideBccIsFalse)
{
  MailMessage m = minimalMessage();
  m.email_recipients.bcc.push_back(MailAddress("eve@example.com"));
  const std::string mime = buildMimeMessage(m, /*hide_bcc=*/false);
  EXPECT_NE(mime.find("Bcc: "), std::string::npos);
  EXPECT_NE(mime.find("eve@example.com"), std::string::npos);
}

TEST_F(MimeWriterTest, ReplyToHeaderEmittedWhenSet)
{
  MailMessage m = minimalMessage();
  m.reply_to = MailAddress("noreply@example.com", "No Reply");
  const std::string mime = buildMimeMessage(m);
  EXPECT_NE(mime.find("Reply-To:"), std::string::npos);
  EXPECT_NE(mime.find("noreply@example.com"), std::string::npos);
}

TEST_F(MimeWriterTest, ReplyToHeaderOmittedWhenAbsent)
{
  const std::string mime = buildMimeMessage(minimalMessage());
  EXPECT_EQ(mime.find("Reply-To:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Subject / display-name encoding
// ---------------------------------------------------------------------------

TEST_F(MimeWriterTest, NonAsciiSubjectIsRfc2047Encoded)
{
  MailMessage m = minimalMessage();
  m.subject = "Привет";
  const std::string mime = buildMimeMessage(m);
  // GMime emits =?UTF-8?...?= encoded-words for non-ASCII subjects.
  EXPECT_NE(mime.find("=?"), std::string::npos) << "Non-ASCII subject was not RFC 2047 encoded: " << mime;
  EXPECT_NE(mime.find("UTF-8"), std::string::npos);
}

TEST_F(MimeWriterTest, EmptySubjectStillProducesValidOutput)
{
  MailMessage m = minimalMessage();
  m.subject = "";
  const std::string mime = buildMimeMessage(m);
  // Subject header may be elided or present-but-empty depending on gmime
  // version. The crucial invariant is that the message still parses (i.e.
  // header section terminator is present).
  EXPECT_NE(mime.find("\r\n\r\n"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Threading headers
// ---------------------------------------------------------------------------

TEST_F(MimeWriterTest, InReplyToHeaderEmittedWhenPresent)
{
  MailMessage m = minimalMessage();
  m.email_threading.in_reply_to = "<original@example.com>";
  const std::string mime = buildMimeMessage(m);
  EXPECT_NE(mime.find("In-Reply-To:"), std::string::npos);
  EXPECT_NE(mime.find("<original@example.com>"), std::string::npos);
}

TEST_F(MimeWriterTest, ReferencesHeaderJoinsWithSpaces)
{
  MailMessage m = minimalMessage();
  m.email_threading.references = { "<a@x>", "<b@x>", "<c@x>" };
  const std::string mime = buildMimeMessage(m);
  auto pos = mime.find("References:");
  ASSERT_NE(pos, std::string::npos);
  // All three message-ids should appear.
  EXPECT_NE(mime.find("<a@x>", pos), std::string::npos);
  EXPECT_NE(mime.find("<b@x>", pos), std::string::npos);
  EXPECT_NE(mime.find("<c@x>", pos), std::string::npos);
}

TEST_F(MimeWriterTest, ReferencesHeaderOmittedWhenEmpty)
{
  const std::string mime = buildMimeMessage(minimalMessage());
  EXPECT_EQ(mime.find("References:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Message-ID
// ---------------------------------------------------------------------------

TEST_F(MimeWriterTest, MessageIdContainsSenderDomain)
{
  MailMessage m = minimalMessage();
  m.sender_domain = "aurora.test";
  const std::string mime = buildMimeMessage(m);
  auto pos = mime.find("Message-Id:");
  ASSERT_NE(pos, std::string::npos);
  // The generated Message-ID has the form <localpart@aurora.test>.
  EXPECT_NE(mime.find("@aurora.test", pos), std::string::npos)
      << "Message-Id did not embed sender domain: " << mime.substr(pos, 200);
}

TEST_F(MimeWriterTest, EmptySenderDomainFallsBackToLocalhost)
{
  MailMessage m = minimalMessage();
  m.sender_domain = "";
  const std::string mime = buildMimeMessage(m);
  auto pos = mime.find("Message-Id:");
  ASSERT_NE(pos, std::string::npos);
  EXPECT_NE(mime.find("@localhost", pos), std::string::npos)
      << "Message-Id did not fall back to @localhost when sender_domain "
         "was empty: "
      << mime.substr(pos, 200);
}

// ---------------------------------------------------------------------------
// Body / attachments
// ---------------------------------------------------------------------------

TEST_F(MimeWriterTest, NoAttachmentsProducesSimpleTextPart)
{
  const std::string mime = buildMimeMessage(minimalMessage());
  EXPECT_NE(mime.find("Content-Type: text/plain"), std::string::npos);
  EXPECT_NE(mime.find("charset=UTF-8"), std::string::npos);
  // No multipart wrapper.
  EXPECT_EQ(mime.find("multipart/"), std::string::npos);
}

TEST_F(MimeWriterTest, WithAttachmentProducesMultipartMixed)
{
  // Create a temp file to attach
  fs::path tmp = fs::temp_directory_path() / "aurora-mime-writer-attach.txt";
  {
    std::ofstream ofs(tmp);
    ofs << "attachment body";
  }

  MailMessage m = minimalMessage();
  m.attachments_.emplace_back(tmp);

  const std::string mime = buildMimeMessage(m);

  EXPECT_NE(mime.find("Content-Type: multipart/mixed"), std::string::npos);
  EXPECT_NE(mime.find("Content-Disposition: attachment"), std::string::npos);
  EXPECT_NE(mime.find("filename=") + 1, std::string::size_type(0));
  EXPECT_NE(mime.find("aurora-mime-writer-attach.txt"), std::string::npos);
  EXPECT_NE(mime.find("Content-Transfer-Encoding: base64"), std::string::npos);

  std::error_code ec;
  fs::remove(tmp, ec);
}

TEST_F(MimeWriterTest, MissingAttachmentFileIsTolerated)
{
  // INCONSISTENCY: addAttachment() prints a warning to stderr and silently
  // drops the attachment if the file cannot be opened. Pin so a future
  // "fail loudly" rewrite is intentional. The caller has no way to know
  // that the attachment was dropped.
  MailMessage m = minimalMessage();
  m.attachments_.emplace_back(fs::path("/this/path/should/never/exist/aurora.bin"));

  const std::string mime = buildMimeMessage(m);
  // Multipart wrapper is still emitted because attachments_ is non-empty.
  EXPECT_NE(mime.find("multipart/mixed"), std::string::npos);
  // But the missing attachment must not have leaked into the output.
  EXPECT_EQ(mime.find("aurora.bin"), std::string::npos);
}
