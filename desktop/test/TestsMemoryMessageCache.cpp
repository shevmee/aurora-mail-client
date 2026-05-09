#include <gtest/gtest.h>

#include <Mail/Cache/CachedMessage.hpp>
#include <Mail/Cache/MemoryMessageCache.hpp>
#include <Mail/Cache/MessageKey.hpp>
#include <QByteArray>
#include <QDateTime>
#include <QString>

#include "QtTestSupport.hpp"

using aurora::mail::app::cache::CachedMessage;
using aurora::mail::app::cache::MemoryMessageCache;
using aurora::mail::app::cache::MessageKey;

namespace
{
  CachedMessage makeEntry(MessageKey key, std::size_t bodyChars = 1)
  {
    CachedMessage e;
    e.key = std::move(key);
    e.content.subject = QStringLiteral("S");
    e.content.body = QString(static_cast<int>(bodyChars), QLatin1Char('x'));
    e.cachedAt = QDateTime::currentDateTime();
    e.approximateBytes = CachedMessage::approximateBytesOf(e.content);
    return e;
  }

  MessageKey k(quint32 uid)
  {
    return MessageKey{ QStringLiteral("user@example.com"), QStringLiteral("INBOX"), 1, uid };
  }
}  // namespace

// ---------------------------------------------------------------------------
// Basic put/get/invalidate
// ---------------------------------------------------------------------------

TEST(MemoryMessageCache, MissReturnsNullAndCountsMiss)
{
  MemoryMessageCache cache;
  EXPECT_EQ(cache.tryGet(k(1)), nullptr);
  EXPECT_EQ(cache.stats().misses, 1U);
  EXPECT_EQ(cache.stats().hitsMemory, 0U);
}

TEST(MemoryMessageCache, PutThenGetCountsHit)
{
  MemoryMessageCache cache;
  cache.put(makeEntry(k(1)));
  auto got = cache.tryGet(k(1));
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->key, k(1));
  EXPECT_EQ(cache.stats().hitsMemory, 1U);
  EXPECT_EQ(cache.stats().misses, 0U);
  EXPECT_EQ(cache.stats().puts, 1U);
}

TEST(MemoryMessageCache, PutReplacesExistingEntry)
{
  MemoryMessageCache cache;
  auto a = makeEntry(k(1));
  a.content.subject = QStringLiteral("first");
  cache.put(a);

  auto b = makeEntry(k(1));
  b.content.subject = QStringLiteral("second");
  cache.put(b);

  auto got = cache.tryGet(k(1));
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->content.subject, QStringLiteral("second"));
  EXPECT_EQ(cache.stats().entriesResident, 1U);
}

TEST(MemoryMessageCache, InvalidateDropsEntryAndCounts)
{
  MemoryMessageCache cache;
  cache.put(makeEntry(k(1)));
  cache.invalidate(k(1));
  EXPECT_EQ(cache.tryGet(k(1)), nullptr);
  EXPECT_EQ(cache.stats().invalidations, 1U);
}

TEST(MemoryMessageCache, InvalidateMailboxOnlyDropsMatchingMailbox)
{
  MemoryMessageCache cache;
  CachedMessage inboxEntry = makeEntry(MessageKey{ "u@x.com", "INBOX", 1, 1 });
  CachedMessage sentEntry = makeEntry(MessageKey{ "u@x.com", "Sent", 1, 1 });
  CachedMessage otherAccount = makeEntry(MessageKey{ "v@x.com", "INBOX", 1, 1 });
  cache.put(inboxEntry);
  cache.put(sentEntry);
  cache.put(otherAccount);

  cache.invalidateMailbox(QStringLiteral("u@x.com"), QStringLiteral("INBOX"));

  EXPECT_EQ(cache.tryGet(inboxEntry.key), nullptr);
  EXPECT_NE(cache.tryGet(sentEntry.key), nullptr);
  EXPECT_NE(cache.tryGet(otherAccount.key), nullptr);
}

TEST(MemoryMessageCache, InvalidateAccountDropsAllEntriesForAccount)
{
  MemoryMessageCache cache;
  cache.put(makeEntry(MessageKey{ "u@x.com", "INBOX", 1, 1 }));
  cache.put(makeEntry(MessageKey{ "u@x.com", "Sent", 1, 1 }));
  cache.put(makeEntry(MessageKey{ "v@x.com", "INBOX", 1, 1 }));

  cache.invalidateAccount(QStringLiteral("u@x.com"));

  EXPECT_EQ(cache.tryGet(MessageKey{ "u@x.com", "INBOX", 1, 1 }), nullptr);
  EXPECT_EQ(cache.tryGet(MessageKey{ "u@x.com", "Sent", 1, 1 }), nullptr);
  EXPECT_NE(cache.tryGet(MessageKey{ "v@x.com", "INBOX", 1, 1 }), nullptr);
}

TEST(MemoryMessageCache, ClearWipesAllEntries)
{
  MemoryMessageCache cache;
  for (quint32 i = 1; i <= 5; ++i)
    cache.put(makeEntry(k(i)));
  cache.clear();
  EXPECT_EQ(cache.stats().entriesResident, 0U);
  for (quint32 i = 1; i <= 5; ++i)
    EXPECT_EQ(cache.tryGet(k(i)), nullptr);
}

// ---------------------------------------------------------------------------
// Eviction policy
// ---------------------------------------------------------------------------

TEST(MemoryMessageCache, EvictsOldestWhenEntryBudgetExceeded)
{
  MemoryMessageCache cache(/*maxEntries=*/3, /*maxBytes=*/64ULL * 1024 * 1024);
  cache.put(makeEntry(k(1)));
  cache.put(makeEntry(k(2)));
  cache.put(makeEntry(k(3)));
  ASSERT_EQ(cache.stats().entriesResident, 3U);

  cache.put(makeEntry(k(4)));  // pushes #1 out (it's least recently used)

  EXPECT_EQ(cache.tryGet(k(1)), nullptr);
  EXPECT_NE(cache.tryGet(k(2)), nullptr);
  EXPECT_NE(cache.tryGet(k(3)), nullptr);
  EXPECT_NE(cache.tryGet(k(4)), nullptr);
  EXPECT_GE(cache.stats().evictions, 1U);
}

TEST(MemoryMessageCache, RecentTryGetMakesEntryMostRecentlyUsed)
{
  MemoryMessageCache cache(/*maxEntries=*/3, /*maxBytes=*/64ULL * 1024 * 1024);
  cache.put(makeEntry(k(1)));
  cache.put(makeEntry(k(2)));
  cache.put(makeEntry(k(3)));

  // Touch #1 -> it should now be most recently used; #2 becomes the LRU.
  ASSERT_NE(cache.tryGet(k(1)), nullptr);

  cache.put(makeEntry(k(4)));  // evicts the LRU which is now #2

  EXPECT_NE(cache.tryGet(k(1)), nullptr) << "freshly-touched entry must survive";
  EXPECT_EQ(cache.tryGet(k(2)), nullptr) << "now-LRU entry must be evicted";
  EXPECT_NE(cache.tryGet(k(3)), nullptr);
  EXPECT_NE(cache.tryGet(k(4)), nullptr);
}

TEST(MemoryMessageCache, ByteBudgetEvictsEvenWhenEntryBudgetAllows)
{
  // Each entry is ~200 chars * sizeof(QChar) (= 400 bytes) + overhead. We
  // size the budget so that exactly two entries fit.
  const std::size_t entrySize = 200 * sizeof(QChar);
  MemoryMessageCache cache(/*maxEntries=*/100, /*maxBytes=*/2 * entrySize + 8);

  cache.put(makeEntry(k(1), 200));
  cache.put(makeEntry(k(2), 200));
  EXPECT_LE(cache.stats().bytesResident, 2 * entrySize + 8);

  cache.put(makeEntry(k(3), 200));
  EXPECT_EQ(cache.tryGet(k(1)), nullptr) << "byte budget must evict the oldest entry";
  EXPECT_LE(cache.stats().bytesResident, 2 * entrySize + 8);
  EXPECT_GE(cache.stats().evictions, 1U);
}

TEST(MemoryMessageCache, SetBudgetsClampsCurrentSize)
{
  MemoryMessageCache cache(/*maxEntries=*/10, /*maxBytes=*/64ULL * 1024 * 1024);
  for (quint32 i = 1; i <= 5; ++i)
    cache.put(makeEntry(k(i)));
  ASSERT_EQ(cache.stats().entriesResident, 5U);

  cache.setBudgets(/*maxEntries=*/2, /*maxBytes=*/64ULL * 1024 * 1024);
  EXPECT_EQ(cache.stats().entriesResident, 2U);
  EXPECT_GE(cache.stats().evictions, 3U);
}

// ---------------------------------------------------------------------------
// Pending reservations
// ---------------------------------------------------------------------------

TEST(MemoryMessageCache, ReservePendingIsExclusive)
{
  MemoryMessageCache cache;
  EXPECT_TRUE(cache.reservePending(k(1)));
  EXPECT_FALSE(cache.reservePending(k(1))) << "a second reservation for the same key must fail";
  // Different keys are independent.
  EXPECT_TRUE(cache.reservePending(k(2)));
}

TEST(MemoryMessageCache, ReleasePendingAllowsReReservation)
{
  MemoryMessageCache cache;
  ASSERT_TRUE(cache.reservePending(k(1)));
  cache.releasePending(k(1));
  EXPECT_TRUE(cache.reservePending(k(1)));
}

TEST(MemoryMessageCache, ReleasePendingForUnheldKeyIsNoOp)
{
  MemoryMessageCache cache;
  cache.releasePending(k(99));  // must not crash
  // After a no-op release, a subsequent reservation must still succeed.
  EXPECT_TRUE(cache.reservePending(k(99)));
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

TEST(MemoryMessageCache, StatsTrackHitsMissesPutsEvictions)
{
  MemoryMessageCache cache(/*maxEntries=*/2, /*maxBytes=*/64ULL * 1024 * 1024);

  // Two misses.
  (void)cache.tryGet(k(1));
  (void)cache.tryGet(k(2));

  cache.put(makeEntry(k(1)));
  cache.put(makeEntry(k(2)));

  // Two hits.
  (void)cache.tryGet(k(1));
  (void)cache.tryGet(k(2));

  // One eviction (k(1) gets evicted because it became LRU after the second
  // tryGet bumped k(2) -- wait: tryGet(k(1)) bumped k(1), then
  // tryGet(k(2)) bumped k(2). LRU is now k(1). Adding k(3) evicts k(1).)
  cache.put(makeEntry(k(3)));

  auto stats = cache.stats();
  EXPECT_EQ(stats.misses, 2U);
  EXPECT_EQ(stats.hitsMemory, 2U);
  EXPECT_EQ(stats.puts, 3U);
  EXPECT_EQ(stats.evictions, 1U);
  EXPECT_EQ(stats.entriesResident, 2U);
}

TEST(MemoryMessageCache, StatsBytesResidentTracksLiveEntries)
{
  MemoryMessageCache cache;
  EXPECT_EQ(cache.stats().bytesResident, 0U);

  CachedMessage e = makeEntry(k(1), /*bodyChars=*/100);
  const std::size_t entryBytes = e.approximateBytes;
  cache.put(std::move(e));
  EXPECT_EQ(cache.stats().bytesResident, entryBytes);

  cache.invalidate(k(1));
  EXPECT_EQ(cache.stats().bytesResident, 0U);
}
