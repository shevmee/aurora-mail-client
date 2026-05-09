#include <gtest/gtest.h>

#include <Mail/Cache/MessageKey.hpp>
#include <unordered_map>
#include <unordered_set>

#include "QtTestSupport.hpp"

using aurora::mail::app::cache::MessageKey;
using aurora::mail::app::cache::MessageKeyHash;

namespace
{
  MessageKey
  makeKey(QString account = "user@example.com", QString mailbox = "INBOX", quint32 uidValidity = 1, quint32 uid = 100)
  {
    return MessageKey{ std::move(account), std::move(mailbox), uidValidity, uid };
  }
}  // namespace

TEST(MessageKey, IsValidRequiresAllFields)
{
  EXPECT_FALSE(MessageKey{}.isValid());
  EXPECT_FALSE((MessageKey{ QString(), "INBOX", 1, 1 }).isValid()) << "missing account";
  EXPECT_FALSE((MessageKey{ "u@x.com", QString(), 1, 1 }).isValid()) << "missing mailbox";
  EXPECT_FALSE((MessageKey{ "u@x.com", "INBOX", 0, 1 }).isValid()) << "missing UIDVALIDITY";
  EXPECT_FALSE((MessageKey{ "u@x.com", "INBOX", 1, 0 }).isValid()) << "missing UID";
  EXPECT_TRUE((MessageKey{ "u@x.com", "INBOX", 1, 1 }).isValid());
}

TEST(MessageKey, EqualityMatchesAllFields)
{
  EXPECT_EQ(makeKey(), makeKey());
  EXPECT_NE(makeKey("a@x.com"), makeKey("b@x.com"));
  EXPECT_NE(makeKey("a@x.com", "INBOX"), makeKey("a@x.com", "Sent"));
  EXPECT_NE(makeKey("a@x.com", "INBOX", 1, 1), makeKey("a@x.com", "INBOX", 2, 1));
  EXPECT_NE(makeKey("a@x.com", "INBOX", 1, 1), makeKey("a@x.com", "INBOX", 1, 2));
}

TEST(MessageKey, MailboxMatchIsCaseSensitive)
{
  // INCONSISTENCY DOC: IMAP mailbox names are technically case-insensitive
  // for "INBOX" specifically (RFC 3501 §5.1.3) but case-sensitive for all
  // other names. The cache key compares as plain QString equality, which
  // is byte-identical case-sensitive. If a server ever returns "InBoX"
  // alongside "INBOX" the cache will treat them as two distinct mailboxes.
  // Pin so a future case-insensitive INBOX rewrite is intentional.
  EXPECT_NE(makeKey("u@x.com", "INBOX"), makeKey("u@x.com", "inbox"));
}

TEST(MessageKey, HashIsConsistentWithEquality)
{
  // Equality => same hash. (The reverse is not required; collisions are
  // allowed.) MessageKeyHash is an unordered_map dependency, so this is a
  // hard contract.
  MessageKeyHash hasher;
  EXPECT_EQ(hasher(makeKey()), hasher(makeKey()));
  EXPECT_EQ(hasher(makeKey("a@x.com", "M", 7, 99)), hasher(makeKey("a@x.com", "M", 7, 99)));
}

TEST(MessageKey, HashDistinguishesFieldsInPractice)
{
  // Any one-field change must (in practice) change the hash. We don't
  // *require* this -- it's a probabilistic check that catches a hash
  // function that ignores a field entirely.
  MessageKeyHash hasher;
  const auto base = hasher(makeKey());
  EXPECT_NE(base, hasher(makeKey("other@x.com")));
  EXPECT_NE(base, hasher(makeKey("user@example.com", "Sent")));
  EXPECT_NE(base, hasher(makeKey("user@example.com", "INBOX", 2, 100)));
  EXPECT_NE(base, hasher(makeKey("user@example.com", "INBOX", 1, 999)));
}

TEST(MessageKey, UsableInUnorderedContainers)
{
  std::unordered_map<MessageKey, int, MessageKeyHash> m;
  m[makeKey("a@x.com", "INBOX", 1, 1)] = 11;
  m[makeKey("a@x.com", "INBOX", 1, 2)] = 12;
  m[makeKey("a@x.com", "Sent", 1, 1)] = 21;

  EXPECT_EQ(m.size(), 3U);
  EXPECT_EQ(m.at(makeKey("a@x.com", "INBOX", 1, 1)), 11);
  EXPECT_EQ(m.at(makeKey("a@x.com", "Sent", 1, 1)), 21);

  std::unordered_set<MessageKey, MessageKeyHash> s;
  s.insert(makeKey());
  EXPECT_EQ(s.size(), 1U);
  s.insert(makeKey());
  EXPECT_EQ(s.size(), 1U) << "inserting an equal key must not grow the set";
}

TEST(MessageKey, StdHashSpecialisationMatchesMessageKeyHash)
{
  // MessageKey is also indexable by std::hash; both implementations must
  // agree so that unordered_map<MessageKey, T> (which picks std::hash by
  // default) and unordered_map<MessageKey, T, MessageKeyHash> partition
  // bucket arrays the same way.
  EXPECT_EQ(std::hash<MessageKey>{}(makeKey()), MessageKeyHash{}(makeKey()));
}

TEST(MessageKey, QHashOverloadIsSeeded)
{
  // The free qHash() function delegates to MessageKeyHash and XORs in the
  // seed; pass two different seeds and expect different results.
  const auto h0 = qHash(makeKey(), 0);
  const auto h1 = qHash(makeKey(), 0xDEADBEEFULL);
  EXPECT_NE(h0, h1) << "qHash must mix the seed in";
}
