#include <gtest/gtest.h>

#include <Email/EmailParser.hpp>
#include <Mail/Cache/CachedMessage.hpp>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QIODevice>

#include "QtTestSupport.hpp"

using aurora::mail::app::cache::CachedMessage;
using aurora::mail::app::cache::decodeCachedMessage;
using aurora::mail::app::cache::encodeCachedMessage;
using aurora::mail::app::cache::MessageKey;
using aurora::mail::app::email::AttachmentInfo;
using aurora::mail::app::email::ParsedEmailContent;

namespace
{
  CachedMessage makeEntry()
  {
    CachedMessage e;
    e.key = MessageKey{ "user@example.com", "INBOX", 123, 4567 };
    e.content.subject = QStringLiteral("Test");
    e.content.from = QStringLiteral("Alice <alice@example.com>");
    e.content.body = QStringLiteral("<p>hello</p>");
    e.content.isHtml = true;
    e.cachedAt = QDateTime::fromSecsSinceEpoch(1'700'000'000);
    e.modSeq = 42;
    return e;
  }

  AttachmentInfo makeAttachment(QString name, QByteArray data)
  {
    AttachmentInfo a;
    a.filename = std::move(name);
    a.contentType = QStringLiteral("application/octet-stream");
    a.size = data.size();
    a.data = std::move(data);
    a.isInline = false;
    return a;
  }
}  // namespace

// ---------------------------------------------------------------------------
// approximateBytesOf
// ---------------------------------------------------------------------------

TEST(CachedMessageApproxBytes, EmptyContentIsZero)
{
  ParsedEmailContent c;
  EXPECT_EQ(CachedMessage::approximateBytesOf(c), 0U);
}

TEST(CachedMessageApproxBytes, CountsStringFieldsInUtf16Units)
{
  ParsedEmailContent c;
  c.subject = QStringLiteral("ab");
  c.from = QStringLiteral("c");
  c.body = QStringLiteral("def");
  // (2 + 1 + 3) chars * sizeof(QChar) (2 bytes) = 12 bytes.
  EXPECT_EQ(CachedMessage::approximateBytesOf(c), 6U * sizeof(QChar));
}

TEST(CachedMessageApproxBytes, AddsAttachmentDataAndMetadata)
{
  ParsedEmailContent c;
  c.attachments.append(makeAttachment(QStringLiteral("a.bin"), QByteArray("hello", 5)));
  c.attachments.append(makeAttachment(QStringLiteral("bb.bin"), QByteArray(100, '\0')));

  // ("a.bin" 5 chars + "application/octet-stream" 24 chars) * 2 + 5
  // ("bb.bin" 6 chars + "application/octet-stream" 24 chars) * 2 + 100
  const std::size_t expected = (5U + 24U) * sizeof(QChar) + 5U + (6U + 24U) * sizeof(QChar) + 100U;
  EXPECT_EQ(CachedMessage::approximateBytesOf(c), expected);
}

// ---------------------------------------------------------------------------
// encode / decode round-trip
// ---------------------------------------------------------------------------

TEST(CachedMessageBlob, RoundTripPreservesAllFields)
{
  CachedMessage in = makeEntry();
  in.content.attachments.append(makeAttachment(QStringLiteral("hello.txt"), QByteArray("hi", 2)));

  QByteArray blob = encodeCachedMessage(in);
  ASSERT_FALSE(blob.isEmpty());

  auto out = decodeCachedMessage(blob);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->key, in.key);
  EXPECT_EQ(out->cachedAt, in.cachedAt);
  EXPECT_EQ(out->modSeq, in.modSeq);
  EXPECT_EQ(out->content.subject, in.content.subject);
  EXPECT_EQ(out->content.from, in.content.from);
  EXPECT_EQ(out->content.body, in.content.body);
  EXPECT_EQ(out->content.isHtml, in.content.isHtml);
  ASSERT_EQ(out->content.attachments.size(), 1);
  EXPECT_EQ(out->content.attachments[0].filename, QStringLiteral("hello.txt"));
  EXPECT_EQ(out->content.attachments[0].data, QByteArray("hi", 2));
  EXPECT_EQ(out->content.attachments[0].size, 2);

  // approximateBytes is recomputed by the decoder.
  EXPECT_EQ(out->approximateBytes, CachedMessage::approximateBytesOf(in.content));
}

TEST(CachedMessageBlob, RoundTripWithNoAttachments)
{
  CachedMessage in = makeEntry();
  ASSERT_TRUE(in.content.attachments.isEmpty());

  auto out = decodeCachedMessage(encodeCachedMessage(in));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->content.attachments.size(), 0);
  EXPECT_EQ(out->content.subject, in.content.subject);
}

TEST(CachedMessageBlob, RoundTripWithBinaryAttachment)
{
  CachedMessage in = makeEntry();
  QByteArray bin;
  bin.reserve(256);
  for (int i = 0; i < 256; ++i)
    bin.append(static_cast<char>(i));
  in.content.attachments.append(makeAttachment(QStringLiteral("dump.bin"), bin));

  auto out = decodeCachedMessage(encodeCachedMessage(in));
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->content.attachments.size(), 1);
  EXPECT_EQ(out->content.attachments[0].data, bin) << "binary attachment must round-trip byte-for-byte";
}

// ---------------------------------------------------------------------------
// Decoder failure modes
// ---------------------------------------------------------------------------

TEST(CachedMessageBlob, DecodeRejectsEmpty)
{
  EXPECT_FALSE(decodeCachedMessage(QByteArray{}).has_value());
}

TEST(CachedMessageBlob, DecodeRejectsBadMagic)
{
  CachedMessage in = makeEntry();
  QByteArray blob = encodeCachedMessage(in);
  ASSERT_GT(blob.size(), 4);
  // The first 4 bytes are the magic. Flip them.
  blob[0] = 'X';
  blob[1] = 'X';
  blob[2] = 'X';
  blob[3] = 'X';
  EXPECT_FALSE(decodeCachedMessage(blob).has_value());
}

TEST(CachedMessageBlob, DecodeRejectsBadVersion)
{
  CachedMessage in = makeEntry();
  QByteArray blob = encodeCachedMessage(in);
  ASSERT_GT(blob.size(), 6);
  // Version is the next 2 bytes after the magic. Bump the LSB.
  blob[5] = static_cast<char>(blob[5] + 1);
  EXPECT_FALSE(decodeCachedMessage(blob).has_value());
}

TEST(CachedMessageBlob, DecodeRejectsTruncatedTail)
{
  CachedMessage in = makeEntry();
  in.content.attachments.append(makeAttachment(QStringLiteral("a.bin"), QByteArray("xyz", 3)));
  QByteArray blob = encodeCachedMessage(in);
  // Drop the last byte -- the attachment payload's tail is now missing,
  // so QDataStream will go into a non-Ok state.
  blob.chop(1);
  EXPECT_FALSE(decodeCachedMessage(blob).has_value());
}

TEST(CachedMessageBlob, DecodeRejectsAttachmentCountAboveLimit)
{
  // Manually craft a blob whose attachment count exceeds the 1024 cap so
  // that decodeCachedMessage() bails out before allocating an absurd
  // QVector. This guards against malicious / corrupted on-disk blobs.
  QByteArray buffer;
  QDataStream out(&buffer, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_6_0);
  out << static_cast<quint32>(0x414D4331u);  // magic 'AMC1'
  out << static_cast<quint16>(1);            // version
  out << QString("u@x.com");
  out << QString("INBOX");
  out << static_cast<quint32>(1);
  out << static_cast<quint32>(1);
  out << QDateTime::currentDateTime();
  out << static_cast<quint64>(0);
  out << QString();                   // subject
  out << QString();                   // from
  out << QString();                   // body
  out << bool(false);                 // isHtml
  out << static_cast<quint32>(2000);  // claim 2000 attachments

  EXPECT_FALSE(decodeCachedMessage(buffer).has_value()) << "Decoder must refuse pathological attachment counts to prevent "
                                                           "memory-exhaustion via crafted cache blobs.";
}
