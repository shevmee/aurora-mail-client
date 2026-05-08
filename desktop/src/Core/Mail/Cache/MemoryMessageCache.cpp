#include "MemoryMessageCache.hpp"

#include <algorithm>
#include <utility>

namespace aurora::mail::app::cache
{

  MemoryMessageCache::MemoryMessageCache(std::size_t maxEntries, std::size_t maxBytes)
      : maxEntries_(maxEntries == 0 ? 1 : maxEntries),
        maxBytes_(maxBytes == 0 ? 1 : maxBytes)
  {
  }

  std::shared_ptr<const CachedMessage> MemoryMessageCache::tryGet(const MessageKey& key)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end())
    {
      misses_.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
    // Splice the just-touched key to the front of the recency list. O(1) on std::list
    // and the iterator stays valid, so the map row does not need updating.
    recency_.splice(recency_.begin(), recency_, it->second.recency);
    hits_.fetch_add(1, std::memory_order_relaxed);
    return it->second.value;
  }

  void MemoryMessageCache::put(CachedMessage entry)
  {
    // Compute size now (cheap; would be wrong to defer once we hold the lock, since
    // approximateBytes is needed by eviction).
    if (entry.approximateBytes == 0)
    {
      entry.approximateBytes = CachedMessage::approximateBytesOf(entry.content);
    }

    auto value = std::make_shared<const CachedMessage>(std::move(entry));
    const MessageKey& key = value->key;

    std::lock_guard<std::mutex> lock(mutex_);
    puts_.fetch_add(1, std::memory_order_relaxed);

    if (auto existing = entries_.find(key); existing != entries_.end())
    {
      // Replace in place: drop old bytes, splice old recency entry to front.
      bytesResident_ -= std::min(bytesResident_, existing->second.value->approximateBytes);
      existing->second.value = value;
      recency_.splice(recency_.begin(), recency_, existing->second.recency);
      bytesResident_ += value->approximateBytes;
    }
    else
    {
      recency_.push_front(key);
      Entry e{ value, recency_.begin() };
      entries_.emplace(key, std::move(e));
      bytesResident_ += value->approximateBytes;
    }

    evictUntilWithinBudget_locked();
  }

  void MemoryMessageCache::invalidate(const MessageKey& key)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.find(key) == entries_.end())
    {
      return;
    }
    eraseEntry_locked(key);
    invalidations_.fetch_add(1, std::memory_order_relaxed);
  }

  void MemoryMessageCache::invalidateMailbox(const QString& accountId, const QString& mailbox)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t removed = 0;

    // Iterate the recency list because we want stable order; collect first to avoid
    // mutating-while-iterating.
    std::vector<MessageKey> doomed;
    doomed.reserve(entries_.size() / 4);
    for (const auto& k : recency_)
    {
      if (k.accountId == accountId && k.mailbox == mailbox)
      {
        doomed.push_back(k);
      }
    }
    for (const auto& k : doomed)
    {
      eraseEntry_locked(k);
      ++removed;
    }
    if (removed != 0)
    {
      invalidations_.fetch_add(removed, std::memory_order_relaxed);
    }
  }

  void MemoryMessageCache::invalidateAccount(const QString& accountId)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t removed = 0;

    std::vector<MessageKey> doomed;
    doomed.reserve(entries_.size() / 2);
    for (const auto& k : recency_)
    {
      if (k.accountId == accountId)
      {
        doomed.push_back(k);
      }
    }
    for (const auto& k : doomed)
    {
      eraseEntry_locked(k);
      ++removed;
    }
    if (removed != 0)
    {
      invalidations_.fetch_add(removed, std::memory_order_relaxed);
    }
  }

  void MemoryMessageCache::clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto count = static_cast<std::uint64_t>(entries_.size());
    entries_.clear();
    recency_.clear();
    bytesResident_ = 0;
    if (count != 0)
    {
      invalidations_.fetch_add(count, std::memory_order_relaxed);
    }
  }

  bool MemoryMessageCache::reservePending(const MessageKey& key)
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    return pending_.insert(key).second;
  }

  void MemoryMessageCache::releasePending(const MessageKey& key)
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pending_.erase(key);
  }

  CacheStats MemoryMessageCache::stats() const
  {
    CacheStats s;
    s.hitsMemory = hits_.load(std::memory_order_relaxed);
    s.misses = misses_.load(std::memory_order_relaxed);
    s.puts = puts_.load(std::memory_order_relaxed);
    s.evictions = evictions_.load(std::memory_order_relaxed);
    s.invalidations = invalidations_.load(std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      s.bytesResident = bytesResident_;
      s.entriesResident = entries_.size();
    }
    return s;
  }

  void MemoryMessageCache::setBudgets(std::size_t maxEntries, std::size_t maxBytes)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    maxEntries_ = maxEntries == 0 ? 1 : maxEntries;
    maxBytes_ = maxBytes == 0 ? 1 : maxBytes;
    evictUntilWithinBudget_locked();
  }

  void MemoryMessageCache::evictUntilWithinBudget_locked()
  {
    while (!recency_.empty() && (entries_.size() > maxEntries_ || bytesResident_ > maxBytes_))
    {
      const MessageKey oldest = recency_.back();
      eraseEntry_locked(oldest);
      evictions_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void MemoryMessageCache::eraseEntry_locked(const MessageKey& key)
  {
    auto it = entries_.find(key);
    if (it == entries_.end())
    {
      return;
    }
    bytesResident_ -= std::min(bytesResident_, it->second.value->approximateBytes);
    recency_.erase(it->second.recency);
    entries_.erase(it);
  }

}  // namespace aurora::mail::app::cache
