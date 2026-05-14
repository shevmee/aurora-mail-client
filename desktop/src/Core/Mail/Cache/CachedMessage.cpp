#include "CachedMessage.hpp"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>

namespace aurora::mail::app::cache
{

  namespace
  {
    // 'A','M','C','1' — Aurora Mail Cache, format family v1.
    constexpr quint32 kBlobMagic = 0x414D4331u;
    constexpr quint16 kBlobVersion = 1;

    // Pin the QDataStream wire format to a single version so cache blobs
    // are byte-identical regardless of whether the build links against Qt 5
    // or Qt 6. Qt_5_15 is the highest version available in Qt 5 and remains
    // a supported reader/writer version in Qt 6.
    constexpr auto kStreamVersion = QDataStream::Qt_5_15;
  }  // namespace

  std::size_t CachedMessage::approximateBytesOf(const aurora::mail::app::email::ParsedEmailContent& content) noexcept
  {
    // QString stores UTF-16 internally; size() is in QChar units. We approximate
    // with size() * sizeof(QChar) which is also a fair upper bound on the
    // serialised UTF-8 length for ASCII-heavy mail.
    auto qstringBytes = [](const QString& s) -> std::size_t { return static_cast<std::size_t>(s.size()) * sizeof(QChar); };

    std::size_t bytes = qstringBytes(content.subject) + qstringBytes(content.from) + qstringBytes(content.body);
    for (const auto& att : content.attachments)
    {
      bytes += qstringBytes(att.filename);
      bytes += qstringBytes(att.contentType);
      // Attachment bytes dominate. Use the actual buffer size rather than `size`,
      // because `size` is the declared content size and may diverge after decoding.
      bytes += static_cast<std::size_t>(att.data.size());
    }
    return bytes;
  }

  QByteArray encodeCachedMessage(const CachedMessage& entry)
  {
    QByteArray buffer;
    QDataStream out(&buffer, QIODevice::WriteOnly);
    out.setVersion(kStreamVersion);

    // Header: magic + version. Allows the persistent layer to fail closed on garbage.
    out << kBlobMagic;
    out << kBlobVersion;

    // Key
    out << entry.key.accountId;
    out << entry.key.mailbox;
    out << entry.key.uidValidity;
    out << entry.key.uid;

    // Bookkeeping
    out << entry.cachedAt;
    out << entry.modSeq;

    // Content
    const auto& c = entry.content;
    out << c.subject;
    out << c.from;
    out << c.body;
    out << c.isHtml;

    // Attachments
    out << static_cast<quint32>(c.attachments.size());
    for (const auto& att : c.attachments)
    {
      out << att.filename;
      out << att.contentType;
      out << static_cast<qint64>(att.size);
      out << att.data;
      out << att.isInline;
    }

    return buffer;
  }

  std::optional<CachedMessage> decodeCachedMessage(const QByteArray& blob)
  {
    QDataStream in(blob);
    in.setVersion(kStreamVersion);

    quint32 magic = 0;
    quint16 version = 0;
    in >> magic >> version;
    if (in.status() != QDataStream::Ok || magic != kBlobMagic || version != kBlobVersion)
    {
      return std::nullopt;
    }

    CachedMessage entry;
    in >> entry.key.accountId;
    in >> entry.key.mailbox;
    in >> entry.key.uidValidity;
    in >> entry.key.uid;

    in >> entry.cachedAt;
    in >> entry.modSeq;

    in >> entry.content.subject;
    in >> entry.content.from;
    in >> entry.content.body;
    in >> entry.content.isHtml;

    quint32 attCount = 0;
    in >> attCount;
    if (in.status() != QDataStream::Ok)
    {
      return std::nullopt;
    }

    // Reasonable upper bound on attachment count to avoid pathological allocation
    // from a corrupt/forged blob. Real-world emails almost never exceed a few dozen.
    constexpr quint32 kMaxAttachments = 1024;
    if (attCount > kMaxAttachments)
    {
      return std::nullopt;
    }

    entry.content.attachments.reserve(static_cast<int>(attCount));
    for (quint32 i = 0; i < attCount; ++i)
    {
      aurora::mail::app::email::AttachmentInfo att;
      qint64 declaredSize = 0;
      in >> att.filename;
      in >> att.contentType;
      in >> declaredSize;
      in >> att.data;
      in >> att.isInline;
      att.size = declaredSize;
      if (in.status() != QDataStream::Ok)
      {
        return std::nullopt;
      }
      entry.content.attachments.append(std::move(att));
    }

    if (in.status() != QDataStream::Ok)
    {
      return std::nullopt;
    }

    entry.approximateBytes = CachedMessage::approximateBytesOf(entry.content);
    return entry;
  }

}  // namespace aurora::mail::app::cache
