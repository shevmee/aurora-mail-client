#ifndef AURORA_MAIL_CACHE_PERSISTENT_MESSAGE_CACHE_HPP
#define AURORA_MAIL_CACHE_PERSISTENT_MESSAGE_CACHE_HPP

#include "AesGcmCipher.hpp"
#include "CachedMessage.hpp"
#include "MessageKey.hpp"

#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

class QSqlDatabase;

namespace aurora::mail::app::cache
{

/**
 * On-disk SQLite tier of the message cache.
 *
 * Layout:
 *   - One database file per account, located under
 *     QStandardPaths::AppLocalDataLocation/messagecache/<sha256(accountId)>.db
 *     created with mode 0600 (POSIX) so other local users cannot read it.
 *   - Schema:
 *
 *       CREATE TABLE messages (
 *           account_id   TEXT    NOT NULL,
 *           mailbox      TEXT    NOT NULL,
 *           uid_validity INTEGER NOT NULL,
 *           uid          INTEGER NOT NULL,
 *           cached_at    INTEGER NOT NULL,
 *           mod_seq      INTEGER NOT NULL DEFAULT 0,
 *           size_bytes   INTEGER NOT NULL,
 *           payload      BLOB    NOT NULL,    -- AES-256-GCM(nonce||ct||tag)
 *           PRIMARY KEY (account_id, mailbox, uid_validity, uid)
 *       );
 *       CREATE INDEX idx_msg_lru ON messages(cached_at);
 *
 * The `payload` blob is the encoded CachedMessage (see CachedMessage.{h,cpp})
 * sealed with AES-256-GCM under a per-account key supplied at construction.
 *
 * Threading: all entry points are thread-safe. Writes acquire a single mutex;
 * SQLite is opened with WAL journal mode so concurrent reads do not block
 * concurrent writes. Each thread that touches the DB binds a per-thread Qt
 * SQL connection (Qt requires this).
 *
 * Eviction: byte-budgeted LRU on (cached_at). Triggered after every put() and
 * lazily on misses.
 *
 * Failure mode: any Qt SQL error logs and degrades to a no-op for that
 * operation; the in-memory tier is unaffected and the user can keep working.
 */
class PersistentMessageCache
{
public:
    /// Default 256 MiB on-disk budget. Tunable via setMaxBytes().
    static constexpr std::uint64_t kDefaultMaxBytes = 256ULL * 1024 * 1024;

    /**
     * Open or create the cache for `accountId` using `key` as the AES-256-GCM
     * master key. Failure to open the DB or apply migrations leaves the object
     * in a "disabled" state where every operation is a successful no-op.
     */
    PersistentMessageCache(QString accountId, AesGcmCipher::Key key, std::uint64_t maxBytes = kDefaultMaxBytes);

    PersistentMessageCache(const PersistentMessageCache&) = delete;
    PersistentMessageCache& operator=(const PersistentMessageCache&) = delete;
    PersistentMessageCache(PersistentMessageCache&&) = delete;
    PersistentMessageCache& operator=(PersistentMessageCache&&) = delete;

    ~PersistentMessageCache();

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    [[nodiscard]] const QString& accountId() const noexcept { return accountId_; }

    [[nodiscard]] std::optional<CachedMessage> tryGet(const MessageKey& key);

    void put(const CachedMessage& entry);

    void invalidate(const MessageKey& key);

    void invalidateMailbox(const QString& mailbox);

    /// Closes the database and unlinks the underlying file. Idempotent.
    void destroy();

    void flush();

private:
    /// Returns the per-thread Qt SQL connection name; opens it if necessary.
    QSqlDatabase ensureConnection();

    void enforceBudget_locked();

    static QString filePathFor(const QString& accountId);
    static QString connectionNameFor(const QString& accountId);

    QString accountId_;
    QString filePath_;
    QString baseConnectionName_;
    AesGcmCipher::Key key_{};
    std::uint64_t maxBytes_;

    bool enabled_ = false;

    // Single writer mutex. SQLite WAL handles read concurrency by itself.
    std::mutex writeMutex_;
};

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_PERSISTENT_MESSAGE_CACHE_HPP
