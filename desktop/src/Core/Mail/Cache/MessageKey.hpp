#ifndef AURORA_MAIL_CACHE_MESSAGE_KEY_HPP
#define AURORA_MAIL_CACHE_MESSAGE_KEY_HPP

#include <QHash>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace aurora::mail::app::cache
{

  /**
   * Strongly-typed cache key for a single IMAP message.
   *
   * UIDs are only meaningful inside a single (account, mailbox, UIDVALIDITY) tuple
   * (RFC 3501 §2.3.1.1). Carrying all four fields by type makes it impossible to
   * accidentally mix messages from two accounts or to reuse a UID across a server
   * UIDVALIDITY change.
   *
   * Plain value type. Cheap to copy thanks to QString implicit sharing.
   */
  struct MessageKey
  {
    /// Stable account identifier; the user's email address is the natural choice.
    QString accountId;

    /// Server-side mailbox path, exactly as returned by LIST (no normalisation).
    QString mailbox;

    /// IMAP UIDVALIDITY (RFC 3501) for the (account, mailbox) at the time of caching.
    quint32 uidValidity = 0;

    /// IMAP UID inside the (account, mailbox, uidValidity).
    quint32 uid = 0;

    [[nodiscard]] bool isValid() const noexcept
    {
      return !accountId.isEmpty() && !mailbox.isEmpty() && uidValidity != 0 && uid != 0;
    }

    bool operator==(const MessageKey& other) const noexcept
    {
      return uid == other.uid && uidValidity == other.uidValidity && mailbox == other.mailbox &&
             accountId == other.accountId;
    }

    bool operator!=(const MessageKey& other) const noexcept
    {
      return !(*this == other);
    }
  };

  /// Hash combiner usable with std::unordered_map / std::unordered_set.
  struct MessageKeyHash
  {
    std::size_t operator()(const MessageKey& key) const noexcept
    {
      // qHash uses Qt's secure-seeded hash (resistant to collision DoS by default).
      std::size_t h = qHash(key.accountId);
      h = h * 0x9E3779B97F4A7C15ULL + qHash(key.mailbox);
      h = h * 0x9E3779B97F4A7C15ULL + key.uidValidity;
      h = h * 0x9E3779B97F4A7C15ULL + key.uid;
      return h;
    }
  };

}  // namespace aurora::mail::app::cache

// Allow the key in QHash-based containers as well (handy for QSet/QHash debug paths).
inline std::size_t qHash(const aurora::mail::app::cache::MessageKey& key, std::size_t seed = 0) noexcept
{
  return aurora::mail::app::cache::MessageKeyHash{}(key) ^ seed;
}

namespace std
{
  template<>
  struct hash<aurora::mail::app::cache::MessageKey>
  {
    std::size_t operator()(const aurora::mail::app::cache::MessageKey& key) const noexcept
    {
      return aurora::mail::app::cache::MessageKeyHash{}(key);
    }
  };
}  // namespace std

#endif  // AURORA_MAIL_CACHE_MESSAGE_KEY_HPP
