#ifndef AURORA_MAIL_CACHE_IMESSAGE_CACHE_HPP
#define AURORA_MAIL_CACHE_IMESSAGE_CACHE_HPP

#include <QString>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "CachedMessage.hpp"
#include "MessageKey.hpp"

namespace aurora::mail::app::cache
{

  /**
   * Read-only counters describing recent cache activity.
   * Returned by IMessageCache::stats(). Cheap to read; values are atomically loaded.
   */
  struct CacheStats
  {
    std::uint64_t hitsMemory = 0;       ///< tryGet served from in-memory tier
    std::uint64_t hitsPersistent = 0;   ///< tryGet served from on-disk tier (and promoted)
    std::uint64_t misses = 0;           ///< tryGet returned nullptr
    std::uint64_t puts = 0;             ///< put() calls
    std::uint64_t evictions = 0;        ///< entries dropped due to budget pressure
    std::uint64_t invalidations = 0;    ///< entries dropped via invalidate*()
    std::uint64_t bytesResident = 0;    ///< approximate bytes currently in the in-memory tier
    std::uint64_t entriesResident = 0;  ///< number of entries currently in the in-memory tier
  };

  /**
   * Cache facade for parsed message bodies.
   *
   * Threading contract: every public method is thread-safe. Concrete implementations
   * are expected to be invoked from both the Qt UI thread and io_context coroutine
   * continuations; callers must NOT assume single-threaded access.
   *
   * Returned values are std::shared_ptr<const CachedMessage> so reads do not deep-copy
   * potentially large attachment payloads under any internal lock.
   */
  class IMessageCache
  {
   public:
    virtual ~IMessageCache() = default;

    /**
     * @return The cached entry for `key`, or nullptr if absent.
     *
     * Implementations may transparently fault from a slower tier on a hit.
     */
    [[nodiscard]] virtual std::shared_ptr<const CachedMessage> tryGet(const MessageKey& key) = 0;

    /**
     * Insert or replace the entry for `entry.key`.
     *
     * Implementations are free to evict to honour byte/entry budgets and to
     * propagate the write asynchronously to a persistent tier.
     */
    virtual void put(CachedMessage entry) = 0;

    /// EXPUNGE / explicit delete / message moved out of source mailbox.
    virtual void invalidate(const MessageKey& key) = 0;

    /// UIDVALIDITY change, full server-side resync, mailbox rebuild.
    virtual void invalidateMailbox(const QString& accountId, const QString& mailbox) = 0;

    /// Sign-out, account removed, credentials revoked.
    /// Persistent implementations MUST destroy on-disk state for this account.
    virtual void invalidateAccount(const QString& accountId) = 0;

    /// Drop everything (tests, panic path).
    virtual void clear() = 0;

    /**
     * Try to mark @p key as "in-flight": a network fetch is in progress for it.
     *
     * Subsumes the ad-hoc m_pendingEmailBodyKey field that previously lived in the
     * UI: callers reserve before issuing a UID FETCH, and any concurrent reservation
     * for the same key is rejected (returns false), so duplicate user clicks coalesce
     * into a single network round-trip.
     *
     * Returns true if the caller now owns the in-flight slot; false if another
     * caller already holds it. Always release with releasePending().
     */
    [[nodiscard]] virtual bool reservePending(const MessageKey& key) = 0;

    /// Release a slot reserved by reservePending(). No-op if not held.
    virtual void releasePending(const MessageKey& key) = 0;

    /// Snapshot of current statistics. Cheap; never blocks on cache work.
    [[nodiscard]] virtual CacheStats stats() const = 0;

    /**
     * Best-effort flush of any pending writes to the persistent tier (if any).
     * Memory-only implementations may no-op. Safe to call on shutdown.
     */
    virtual void flush()
    {
    }
  };

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_IMESSAGE_CACHE_HPP
