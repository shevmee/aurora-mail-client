#include <gtest/gtest.h>

#include <ImapUtf7.hpp>

using aurora::mail::common::utils::decodeImapUtf7;
using aurora::mail::common::utils::encodeImapUtf7;

namespace
{
  void expectRoundTrip(const std::string& utf8)
  {
    const std::string encoded = encodeImapUtf7(utf8);
    const std::string decoded = decodeImapUtf7(encoded);
    EXPECT_EQ(decoded, utf8) << "Round trip failed for: " << utf8 << " (encoded as: " << encoded << ")";
  }
}  // namespace

TEST(ImapUtf7, EmptyString)
{
  EXPECT_EQ(decodeImapUtf7(""), "");
  EXPECT_EQ(encodeImapUtf7(""), "");
}

TEST(ImapUtf7, AsciiPassesThroughUnchanged)
{
  EXPECT_EQ(decodeImapUtf7("INBOX"), "INBOX");
  EXPECT_EQ(encodeImapUtf7("INBOX"), "INBOX");
  EXPECT_EQ(encodeImapUtf7("Sent"), "Sent");
  EXPECT_EQ(encodeImapUtf7("Drafts"), "Drafts");
}

TEST(ImapUtf7, AmpersandIsEscapedToAmpDash)
{
  EXPECT_EQ(encodeImapUtf7("AT&T"), "AT&-T");
  EXPECT_EQ(decodeImapUtf7("AT&-T"), "AT&T");
}

TEST(ImapUtf7, TrailingAmpersandIsEscaped)
{
  EXPECT_EQ(encodeImapUtf7("Trailing&"), "Trailing&-");
  EXPECT_EQ(decodeImapUtf7("Trailing&-"), "Trailing&");
}

TEST(ImapUtf7, LiteralAmpersandRoundTrips)
{
  expectRoundTrip("AT&T");
  expectRoundTrip("&");
  expectRoundTrip("a&b&c");
}

TEST(ImapUtf7, RussianFolderNameRoundTrips)
{
  expectRoundTrip("Помеченные");
  expectRoundTrip("[Gmail]/Вся почта");
}

TEST(ImapUtf7, ChineseFolderNameRoundTrips)
{
  expectRoundTrip("世界");
  expectRoundTrip("收件箱");
}

TEST(ImapUtf7, DecodesKnownGmailEncoding)
{
  // From RFC 3501 §5.1.3 – Gmail's Russian "Вся почта".
  EXPECT_EQ(decodeImapUtf7("[Gmail]/&BBIEQQRP- &BD8EPgRHBEIEMA-"), "[Gmail]/Вся почта");
}

TEST(ImapUtf7, MalformedTrailingAmpersandTreatedAsLiteral)
{
  // A '&' with no closing '-' is a parse error in real IMAP, but the helper
  // is defensive: it falls back to treating '&' as a literal so the caller
  // can still display the folder name.
  EXPECT_EQ(decodeImapUtf7("Bogus&"), "Bogus&");
}

TEST(ImapUtf7, MultipleEncodedSegments)
{
  expectRoundTrip("Папка/Sent/Чернетки");
}

TEST(ImapUtf7, NonAsciiAtStartAndEnd)
{
  expectRoundTrip("ÆBC");
  expectRoundTrip("ABCÆ");
}

TEST(ImapUtf7, AllAsciiPrintableExceptAmpRoundTrips)
{
  // 0x20..0x7E except '&' must be passed through verbatim.
  std::string ascii;
  for (int c = 0x20; c <= 0x7E; ++c)
  {
    if (c == '&')
      continue;
    ascii.push_back(static_cast<char>(c));
  }
  EXPECT_EQ(encodeImapUtf7(ascii), ascii);
  EXPECT_EQ(decodeImapUtf7(ascii), ascii);
}

TEST(ImapUtf7, EncodedOutputDoesNotUseSlash)
{
  // IMAP modified UTF-7 replaces '/' with ',' inside the encoded segment.
  const std::string encoded = encodeImapUtf7("Помеченные");
  // Inside &…- segments there must be no '/'; the '/' may appear only as a
  // literal folder separator.
  size_t amp = encoded.find('&');
  ASSERT_NE(amp, std::string::npos);
  size_t dash = encoded.find('-', amp);
  ASSERT_NE(dash, std::string::npos);
  EXPECT_EQ(encoded.find('/', amp), std::string::npos) << "encoded segment contains a literal '/': " << encoded;
}

TEST(ImapUtf7, EncodedOutputHasNoBase64Padding)
{
  const std::string encoded = encodeImapUtf7("Sent_Папка");
  EXPECT_EQ(encoded.find('='), std::string::npos) << "encoded form must not contain '=' padding: " << encoded;
}
