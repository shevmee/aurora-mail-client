#ifndef AURORA_MAIL_CACHE_CACHED_MESSAGE_HPP
#define AURORA_MAIL_CACHE_CACHED_MESSAGE_HPP

#include "Email/EmailParser.hpp"
#include "MessageKey.hpp"

#include <QByteArray>
#include <QDateTime>

#include <cstddef>
#include <cstdint>

namespace aurora::mail::app::cache
{

/**
 * Envelope around a parsed message body that the cache stores and the UI consumes.
 *
 * The envelope captures everything required for byte-budgeted eviction, age-based
 * refresh, and CONDSTORE-style delta sync — none of which the raw
 * ParsedEmailContent provides.
 */
struct CachedMessage
{
    MessageKey key;

    /// Parsed and ready-to-render message content (subject/from/body/attachments).
    /// Stored as part of the envelope so the cache returns one immutable value.
    aurora::mail::app::email::ParsedEmailContent content;

    /// Approximate in-memory footprint (body + attachment bodies, in bytes).
    /// Used by MemoryMessageCache to enforce a byte budget on top of an entry budget.
    std::size_t approximateBytes = 0;

    /// When this envelope was first created. Drives age-based refresh and on-disk LRU.
    QDateTime cachedAt;

    /// Optional CONDSTORE MODSEQ for the underlying message; 0 if unknown.
    quint64 modSeq = 0;

    /// Compute the approximate footprint of a parsed body in bytes.
    /// Counts the UTF-8 length of QStrings and the byte length of attachment payloads.
    [[nodiscard]] static std::size_t approximateBytesOf(
        const aurora::mail::app::email::ParsedEmailContent& content) noexcept;
};

/**
 * Serialise a CachedMessage envelope into a single binary blob.
 *
 * Format is versioned (4-byte magic + 2-byte version + QDataStream payload) so that
 * the persistent tier can detect and reject blobs from incompatible versions.
 */
[[nodiscard]] QByteArray encodeCachedMessage(const CachedMessage& entry);

/// Inverse of encodeCachedMessage. Returns std::nullopt on any framing/version error.
[[nodiscard]] std::optional<CachedMessage> decodeCachedMessage(const QByteArray& blob);

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_CACHED_MESSAGE_HPP
