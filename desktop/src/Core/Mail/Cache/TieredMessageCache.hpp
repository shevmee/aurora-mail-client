#ifndef AURORA_MAIL_CACHE_TIERED_MESSAGE_CACHE_HPP
#define AURORA_MAIL_CACHE_TIERED_MESSAGE_CACHE_HPP

#include <QString>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "IMessageCache.hpp"
#include "MemoryMessageCache.hpp"
#include "PersistentMessageCache.hpp"

namespace aurora::mail::app::cache
{

  /**
   * Two-tier IMessageCache:
   *   - Tier 1: MemoryMessageCache (LRU, byte-budgeted, shared_ptr reads).
   *   - Tier 2: per-account PersistentMessageCache (SQLite + AES-256-GCM).
   *
   * Lookup flow:
   *   tryGet -> memory; on miss, fall through to persistent; on persistent hit,
   *   promote to memory and serve. tryGet is the only path that touches the
   *   persistent tier on the read side, keeping the hot path single-mutex.
   *
   * Write flow:
   *   put writes to memory synchronously and to the persistent tier inline (the
   *   persistent tier is itself protected by a mutex, so the call returns once
   *   SQLite has accepted the row). This keeps the implementation simple and
   *   bounded; if persistence latency ever shows up in profiles, the persistent
   *   put can be moved behind a single-thread executor without changing the API.
   *
   * Invalidation flow:
   *   Every invalidate*() fans out to both tiers. invalidateAccount() additionally
   *   shreds the on-disk file and destroys the per-account master key, which is
   *   the only correct behaviour for a sign-out.
   *
   * Per-account persistence is opt-in: callers attach a PersistentMessageCache via
   * attachAccount(); accounts without an attached persistent tier silently fall
   * back to memory-only behaviour.
   */
  class TieredMessageCache final : public IMessageCache
  {
   public:
    explicit TieredMessageCache(
        std::size_t memMaxEntries = MemoryMessageCache::kDefaultMaxEntries,
        std::size_t memMaxBytes = MemoryMessageCache::kDefaultMaxBytes);

    ~TieredMessageCache() override = default;

    TieredMessageCache(const TieredMessageCache&) = delete;
    TieredMessageCache& operator=(const TieredMessageCache&) = delete;
    TieredMessageCache(TieredMessageCache&&) = delete;
    TieredMessageCache& operator=(TieredMessageCache&&) = delete;

    // IMessageCache
    [[nodiscard]] std::shared_ptr<const CachedMessage> tryGet(const MessageKey& key) override;
    void put(CachedMessage entry) override;
    void invalidate(const MessageKey& key) override;
    void invalidateMailbox(const QString& accountId, const QString& mailbox) override;
    void invalidateAccount(const QString& accountId) override;
    void clear() override;

    [[nodiscard]] bool reservePending(const MessageKey& key) override;
    void releasePending(const MessageKey& key) override;

    [[nodiscard]] CacheStats stats() const override;
    void flush() override;

    /**
     * Attach a persistent tier for `accountId`. If a persistent tier already
     * exists for that account it is replaced (the old one is closed). Pass
     * nullptr to detach (memory-only for that account).
     */
    void attachAccount(const QString& accountId, std::unique_ptr<PersistentMessageCache> persistent);

    /// True if a persistent tier is attached for `accountId`.
    [[nodiscard]] bool hasPersistentTier(const QString& accountId) const;

   private:
    PersistentMessageCache* persistentFor_locked(const QString& accountId) const;

    MemoryMessageCache memory_;

    mutable std::mutex tiersMutex_;
    std::unordered_map<QString, std::unique_ptr<PersistentMessageCache>> persistent_;

    std::atomic<std::uint64_t> persistentHits_{ 0 };
  };

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_TIERED_MESSAGE_CACHE_HPP
