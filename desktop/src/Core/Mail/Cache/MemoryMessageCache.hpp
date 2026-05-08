#ifndef AURORA_MAIL_CACHE_MEMORY_MESSAGE_CACHE_HPP
#define AURORA_MAIL_CACHE_MEMORY_MESSAGE_CACHE_HPP

#include "IMessageCache.hpp"
#include "MessageKey.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace aurora::mail::app::cache
{

/**
 * In-memory tier of the message cache.
 *
 * Implementation is a single-map LRU: one std::unordered_map keyed by MessageKey
 * holds (shared_ptr<const CachedMessage>, list_iterator) pairs, and a std::list of
 * keys serves as the recency order. This replaces the previous three-map design
 * (`map_`, `content_`, `lru_`) which required keeping three containers in sync.
 *
 * Eviction is governed by two independent budgets that are BOTH enforced:
 *  - `maxEntries`: count-based (legacy default of 64 retained as a sane floor).
 *  - `maxBytes`:   byte-based, summing CachedMessage::approximateBytes across all
 *                  resident entries. This is the only reliable cap once messages
 *                  carry attachments.
 *
 * tryGet() returns std::shared_ptr<const CachedMessage>: the value graph is shared
 * by reference, so the read path performs no deep copy of attachments under the
 * cache mutex. CachedMessage is treated as immutable once inserted.
 */
class MemoryMessageCache final : public IMessageCache
{
public:
    /// Default cap of 64 entries / 64 MiB matches the previous behaviour for tiny
    /// messages while preventing runaway memory once attachments are involved.
    static constexpr std::size_t kDefaultMaxEntries = 64;
    static constexpr std::size_t kDefaultMaxBytes = 64ULL * 1024 * 1024;  // 64 MiB

    MemoryMessageCache(std::size_t maxEntries = kDefaultMaxEntries, std::size_t maxBytes = kDefaultMaxBytes);

    MemoryMessageCache(const MemoryMessageCache&) = delete;
    MemoryMessageCache& operator=(const MemoryMessageCache&) = delete;
    MemoryMessageCache(MemoryMessageCache&&) = delete;
    MemoryMessageCache& operator=(MemoryMessageCache&&) = delete;

    [[nodiscard]] std::shared_ptr<const CachedMessage> tryGet(const MessageKey& key) override;
    void put(CachedMessage entry) override;
    void invalidate(const MessageKey& key) override;
    void invalidateMailbox(const QString& accountId, const QString& mailbox) override;
    void invalidateAccount(const QString& accountId) override;
    void clear() override;

    [[nodiscard]] bool reservePending(const MessageKey& key) override;
    void releasePending(const MessageKey& key) override;

    [[nodiscard]] CacheStats stats() const override;

    /// Adjust budgets at runtime (clamps current size to the new caps).
    void setBudgets(std::size_t maxEntries, std::size_t maxBytes);

private:
    struct Entry
    {
        std::shared_ptr<const CachedMessage> value;
        std::list<MessageKey>::iterator recency;  // points into recency_
    };

    /// Caller MUST hold mutex_. Drops oldest entries until both budgets hold.
    void evictUntilWithinBudget_locked();

    /// Caller MUST hold mutex_. Removes a single entry, updating bytesResident_.
    void eraseEntry_locked(const MessageKey& key);

    mutable std::mutex mutex_;
    std::list<MessageKey> recency_;  // front = most recently used
    std::unordered_map<MessageKey, Entry, MessageKeyHash> entries_;

    // In-flight tracker for reservePending()/releasePending().
    std::mutex pendingMutex_;
    std::unordered_set<MessageKey, MessageKeyHash> pending_;

    std::size_t maxEntries_;
    std::size_t maxBytes_;
    std::size_t bytesResident_ = 0;

    // Counters are atomic so stats() can be called without taking the cache mutex.
    std::atomic<std::uint64_t> hits_{0};
    std::atomic<std::uint64_t> misses_{0};
    std::atomic<std::uint64_t> puts_{0};
    std::atomic<std::uint64_t> evictions_{0};
    std::atomic<std::uint64_t> invalidations_{0};
};

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_MEMORY_MESSAGE_CACHE_HPP
