#include "TieredMessageCache.hpp"

#include <QDateTime>
#include <utility>

#include "CacheKeyMaterial.hpp"

namespace aurora::mail::app::cache
{

  TieredMessageCache::TieredMessageCache(std::size_t memMaxEntries, std::size_t memMaxBytes)
      : memory_(memMaxEntries, memMaxBytes)
  {
  }

  std::shared_ptr<const CachedMessage> TieredMessageCache::tryGet(const MessageKey& key)
  {
    if (auto hit = memory_.tryGet(key); hit)
    {
      return hit;
    }

    PersistentMessageCache* tier2 = nullptr;
    {
      std::lock_guard<std::mutex> lock(tiersMutex_);
      tier2 = persistentFor_locked(key.accountId);
    }
    if (tier2 == nullptr)
    {
      return nullptr;
    }

    auto disk = tier2->tryGet(key);
    if (!disk.has_value())
    {
      return nullptr;
    }

    persistentHits_.fetch_add(1, std::memory_order_relaxed);

    // Promote into the in-memory tier so subsequent accesses are O(1) again.
    // Take a copy first so we can return a shared_ptr after promoting.
    CachedMessage promoted = *disk;
    memory_.put(promoted);
    // After promotion the canonical shared_ptr lives in the memory tier; fetch it
    // back so callers get the same instance the cache will keep alive.
    if (auto post = memory_.tryGet(key); post)
    {
      return post;
    }
    // Extremely unlikely (immediate eviction): hand the caller a one-shot copy.
    return std::make_shared<const CachedMessage>(std::move(promoted));
  }

  void TieredMessageCache::put(CachedMessage entry)
  {
    PersistentMessageCache* tier2 = nullptr;
    {
      std::lock_guard<std::mutex> lock(tiersMutex_);
      tier2 = persistentFor_locked(entry.key.accountId);
    }

    if (!entry.cachedAt.isValid())
    {
      entry.cachedAt = QDateTime::currentDateTime();
    }

    if (tier2 != nullptr)
    {
      // Persist a copy first; if the seal/insert fails the in-memory tier still
      // gets the value.
      tier2->put(entry);
    }
    memory_.put(std::move(entry));
  }

  void TieredMessageCache::invalidate(const MessageKey& key)
  {
    memory_.invalidate(key);

    PersistentMessageCache* tier2 = nullptr;
    {
      std::lock_guard<std::mutex> lock(tiersMutex_);
      tier2 = persistentFor_locked(key.accountId);
    }
    if (tier2 != nullptr)
    {
      tier2->invalidate(key);
    }
  }

  void TieredMessageCache::invalidateMailbox(const QString& accountId, const QString& mailbox)
  {
    memory_.invalidateMailbox(accountId, mailbox);

    PersistentMessageCache* tier2 = nullptr;
    {
      std::lock_guard<std::mutex> lock(tiersMutex_);
      tier2 = persistentFor_locked(accountId);
    }
    if (tier2 != nullptr)
    {
      tier2->invalidateMailbox(mailbox);
    }
  }

  void TieredMessageCache::invalidateAccount(const QString& accountId)
  {
    memory_.invalidateAccount(accountId);

    std::unique_ptr<PersistentMessageCache> doomed;
    {
      std::lock_guard<std::mutex> lock(tiersMutex_);
      if (auto it = persistent_.find(accountId); it != persistent_.end())
      {
        doomed = std::move(it->second);
        persistent_.erase(it);
      }
    }
    if (doomed)
    {
      // Shred on-disk file and master key. Order matters: delete the file
      // first (renders existing ciphertext useless even before key is gone),
      // then erase the key from the keychain so future runs cannot decrypt
      // anything that survives in backups.
      doomed->destroy();
      doomed.reset();
      CacheKeyMaterial::destroy(accountId);
    }
  }

  void TieredMessageCache::clear()
  {
    memory_.clear();
    std::lock_guard<std::mutex> lock(tiersMutex_);
    for (auto& [_, p] : persistent_)
    {
      if (p)
      {
        p->destroy();
      }
    }
    persistent_.clear();
  }

  bool TieredMessageCache::reservePending(const MessageKey& key)
  {
    return memory_.reservePending(key);
  }

  void TieredMessageCache::releasePending(const MessageKey& key)
  {
    memory_.releasePending(key);
  }

  CacheStats TieredMessageCache::stats() const
  {
    CacheStats s = memory_.stats();
    s.hitsPersistent = persistentHits_.load(std::memory_order_relaxed);
    return s;
  }

  void TieredMessageCache::flush()
  {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    for (auto& [_, p] : persistent_)
    {
      if (p)
      {
        p->flush();
      }
    }
  }

  void TieredMessageCache::attachAccount(const QString& accountId, std::unique_ptr<PersistentMessageCache> persistent)
  {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    if (!persistent)
    {
      persistent_.erase(accountId);
      return;
    }
    persistent_[accountId] = std::move(persistent);
  }

  bool TieredMessageCache::hasPersistentTier(const QString& accountId) const
  {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    auto it = persistent_.find(accountId);
    return it != persistent_.end() && it->second && it->second->isEnabled();
  }

  PersistentMessageCache* TieredMessageCache::persistentFor_locked(const QString& accountId) const
  {
    auto it = persistent_.find(accountId);
    if (it == persistent_.end() || !it->second || !it->second->isEnabled())
    {
      return nullptr;
    }
    return it->second.get();
  }

}  // namespace aurora::mail::app::cache
