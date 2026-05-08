#include "PersistentMessageCache.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QThread>
#include <QVariant>

#include <chrono>

#if defined(Q_OS_UNIX) || defined(__APPLE__)
#include <sys/stat.h>
#elif defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <vector>
#endif

namespace aurora::mail::app::cache
{

namespace
{
constexpr const char* kDriver = "QSQLITE";
constexpr const char* kCacheSubdir = "messagecache";

#if defined(Q_OS_WIN)
// Apply a protected DACL granting only the current user full access to `path`.
// Mirrors the POSIX 0600 semantics that we set on UNIX: nobody but the file
// owner can read/write the cache (NFR-05). PROTECTED_DACL_SECURITY_INFORMATION
// also blocks ACEs inherited from the parent directory.
void restrictPermissionsWin(const QString& path)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        qWarning() << "PersistentMessageCache: OpenProcessToken failed:"
                   << static_cast<unsigned long>(GetLastError());
        return;
    }

    DWORD tokenInfoLen = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenInfoLen);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        qWarning() << "PersistentMessageCache: GetTokenInformation(size) failed:"
                   << static_cast<unsigned long>(GetLastError());
        CloseHandle(token);
        return;
    }
    std::vector<BYTE> tokenBuf(tokenInfoLen);
    if (!GetTokenInformation(token, TokenUser, tokenBuf.data(), tokenInfoLen, &tokenInfoLen))
    {
        qWarning() << "PersistentMessageCache: GetTokenInformation failed:"
                   << static_cast<unsigned long>(GetLastError());
        CloseHandle(token);
        return;
    }
    CloseHandle(token);

    PSID userSid = reinterpret_cast<TOKEN_USER*>(tokenBuf.data())->User.Sid;

    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = FILE_ALL_ACCESS;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = static_cast<LPWSTR>(userSid);

    PACL acl = nullptr;
    DWORD createResult = SetEntriesInAclW(1, &ea, nullptr, &acl);
    if (createResult != ERROR_SUCCESS)
    {
        qWarning() << "PersistentMessageCache: SetEntriesInAcl failed:"
                   << static_cast<unsigned long>(createResult);
        return;
    }

    const std::wstring pathW = path.toStdWString();
    const DWORD setResult = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(pathW.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr);
    if (setResult != ERROR_SUCCESS)
    {
        qWarning() << "PersistentMessageCache: SetNamedSecurityInfo failed:"
                   << static_cast<unsigned long>(setResult);
    }
    LocalFree(acl);
}
#endif

void restrictPermissions(const QString& path)
{
#if defined(Q_OS_UNIX) || defined(__APPLE__)
    // 0600: owner read/write only. The cache contains decryptable plaintext
    // metadata (account_id, mailbox, uid) and AES-GCM ciphertext payloads;
    // even though the body is encrypted, treat the file as sensitive at rest.
    ::chmod(path.toUtf8().constData(), S_IRUSR | S_IWUSR);
#elif defined(Q_OS_WIN)
    restrictPermissionsWin(path);
#else
    Q_UNUSED(path);
#endif
}

}  // namespace

QString PersistentMessageCache::filePathFor(const QString& accountId)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir dir(base);
    if (!dir.exists())
    {
        dir.mkpath(QStringLiteral("."));
    }
    if (!dir.exists(QString::fromLatin1(kCacheSubdir)))
    {
        dir.mkpath(QString::fromLatin1(kCacheSubdir));
    }
    const QByteArray digest = QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256);
    return dir.filePath(QString::fromLatin1(kCacheSubdir) + QLatin1Char('/')
                        + QString::fromLatin1(digest.toHex().left(32)) + QStringLiteral(".db"));
}

QString PersistentMessageCache::connectionNameFor(const QString& accountId)
{
    const QByteArray digest = QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("aurora-msgcache-") + QString::fromLatin1(digest.toHex().left(16));
}

PersistentMessageCache::PersistentMessageCache(QString accountId, AesGcmCipher::Key key, std::uint64_t maxBytes)
    : accountId_(std::move(accountId))
    , filePath_(filePathFor(accountId_))
    , baseConnectionName_(connectionNameFor(accountId_))
    , key_(key)
    , maxBytes_(maxBytes == 0 ? kDefaultMaxBytes : maxBytes)
{
    if (!QSqlDatabase::isDriverAvailable(QString::fromLatin1(kDriver)))
    {
        qWarning() << "PersistentMessageCache: QSQLITE driver missing; cache disabled";
        return;
    }

    auto db = ensureConnection();
    if (!db.isOpen())
    {
        qWarning() << "PersistentMessageCache: failed to open" << filePath_ << ":" << db.lastError().text();
        return;
    }

    restrictPermissions(filePath_);

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA journal_mode=WAL")))
    {
        qWarning() << "PersistentMessageCache: PRAGMA journal_mode=WAL failed:" << q.lastError().text();
    }
    if (!q.exec(QStringLiteral("PRAGMA synchronous=NORMAL")))
    {
        qWarning() << "PersistentMessageCache: PRAGMA synchronous=NORMAL failed:" << q.lastError().text();
    }
    if (!q.exec(QStringLiteral("PRAGMA temp_store=MEMORY")))
    {
        // Non-fatal: just means temp files may hit disk.
    }

    const QString schema = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS messages ("
        "  account_id   TEXT    NOT NULL,"
        "  mailbox      TEXT    NOT NULL,"
        "  uid_validity INTEGER NOT NULL,"
        "  uid          INTEGER NOT NULL,"
        "  cached_at    INTEGER NOT NULL,"
        "  mod_seq      INTEGER NOT NULL DEFAULT 0,"
        "  size_bytes   INTEGER NOT NULL,"
        "  payload      BLOB    NOT NULL,"
        "  PRIMARY KEY (account_id, mailbox, uid_validity, uid)"
        ")");
    if (!q.exec(schema))
    {
        qWarning() << "PersistentMessageCache: CREATE TABLE failed:" << q.lastError().text();
        return;
    }
    if (!q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_msg_lru ON messages(cached_at)")))
    {
        qWarning() << "PersistentMessageCache: CREATE INDEX failed:" << q.lastError().text();
    }

    enabled_ = true;
}

PersistentMessageCache::~PersistentMessageCache()
{
    // Best effort: wipe the in-memory copy of the master key. SQLite connections
    // owned by other threads will be cleaned up by Qt when those threads exit.
    AesGcmCipher::secureWipe(key_);

    // Close the connection bound to *this* thread, if any. Other threads' bindings
    // remain — SQLite handles per-connection lifecycle and they are reusable on
    // re-construction with the same connection name suffix.
    const QString name = baseConnectionName_ + QStringLiteral("-")
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name))
    {
        {
            // RAII scope so the database object is destroyed before removeDatabase().
            QSqlDatabase db = QSqlDatabase::database(name, /*open=*/false);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
}

QSqlDatabase PersistentMessageCache::ensureConnection()
{
    // Qt requires one QSqlDatabase per thread per connection; key the connection
    // name on the current thread id so multiple threads sharing this object each
    // get their own SQLite handle.
    const QString name = baseConnectionName_ + QStringLiteral("-")
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    if (QSqlDatabase::contains(name))
    {
        QSqlDatabase db = QSqlDatabase::database(name, /*open=*/true);
        return db;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QString::fromLatin1(kDriver), name);
    db.setDatabaseName(filePath_);
    if (!db.open())
    {
        qWarning() << "PersistentMessageCache: open failed:" << db.lastError().text();
    }
    return db;
}

std::optional<CachedMessage> PersistentMessageCache::tryGet(const MessageKey& key)
{
    if (!enabled_ || !key.isValid())
    {
        return std::nullopt;
    }
    auto db = ensureConnection();
    if (!db.isOpen())
    {
        return std::nullopt;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT payload FROM messages "
        "WHERE account_id=:a AND mailbox=:m AND uid_validity=:v AND uid=:u"));
    q.bindValue(QStringLiteral(":a"), key.accountId);
    q.bindValue(QStringLiteral(":m"), key.mailbox);
    q.bindValue(QStringLiteral(":v"), static_cast<qulonglong>(key.uidValidity));
    q.bindValue(QStringLiteral(":u"), static_cast<qulonglong>(key.uid));

    if (!q.exec() || !q.next())
    {
        return std::nullopt;
    }

    const QByteArray sealed = q.value(0).toByteArray();
    auto plain = AesGcmCipher::open(key_, sealed);
    if (!plain.has_value())
    {
        // Authentication failed: either the file was tampered with, or the
        // master key is stale (e.g. rotated). Drop the row so we don't keep
        // surfacing it on every access.
        qWarning() << "PersistentMessageCache: AEAD verification failed; dropping row";
        invalidate(key);
        return std::nullopt;
    }
    auto decoded = decodeCachedMessage(*plain);
    if (!decoded.has_value())
    {
        qWarning() << "PersistentMessageCache: blob decode failed; dropping row";
        invalidate(key);
        return std::nullopt;
    }
    return decoded;
}

void PersistentMessageCache::put(const CachedMessage& entry)
{
    if (!enabled_ || !entry.key.isValid())
    {
        return;
    }
    const QByteArray plain = encodeCachedMessage(entry);
    auto sealed = AesGcmCipher::seal(key_, plain);
    if (!sealed.has_value())
    {
        qWarning() << "PersistentMessageCache: AEAD seal failed; entry not persisted";
        return;
    }

    std::lock_guard<std::mutex> lock(writeMutex_);
    auto db = ensureConnection();
    if (!db.isOpen())
    {
        return;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO messages "
        "  (account_id, mailbox, uid_validity, uid, cached_at, mod_seq, size_bytes, payload) "
        "VALUES (:a,:m,:v,:u,:t,:s,:b,:p) "
        "ON CONFLICT(account_id, mailbox, uid_validity, uid) DO UPDATE SET "
        "  cached_at=excluded.cached_at, mod_seq=excluded.mod_seq, "
        "  size_bytes=excluded.size_bytes, payload=excluded.payload"));
    q.bindValue(QStringLiteral(":a"), entry.key.accountId);
    q.bindValue(QStringLiteral(":m"), entry.key.mailbox);
    q.bindValue(QStringLiteral(":v"), static_cast<qulonglong>(entry.key.uidValidity));
    q.bindValue(QStringLiteral(":u"), static_cast<qulonglong>(entry.key.uid));
    const qint64 cachedAtMs = entry.cachedAt.isValid()
        ? entry.cachedAt.toMSecsSinceEpoch()
        : QDateTime::currentMSecsSinceEpoch();
    q.bindValue(QStringLiteral(":t"), cachedAtMs);
    q.bindValue(QStringLiteral(":s"), static_cast<qulonglong>(entry.modSeq));
    q.bindValue(QStringLiteral(":b"), static_cast<qulonglong>(sealed->size()));
    q.bindValue(QStringLiteral(":p"), *sealed);
    if (!q.exec())
    {
        qWarning() << "PersistentMessageCache: insert failed:" << q.lastError().text();
        return;
    }

    enforceBudget_locked();
}

void PersistentMessageCache::invalidate(const MessageKey& key)
{
    if (!enabled_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(writeMutex_);
    auto db = ensureConnection();
    if (!db.isOpen())
    {
        return;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM messages "
        "WHERE account_id=:a AND mailbox=:m AND uid_validity=:v AND uid=:u"));
    q.bindValue(QStringLiteral(":a"), key.accountId);
    q.bindValue(QStringLiteral(":m"), key.mailbox);
    q.bindValue(QStringLiteral(":v"), static_cast<qulonglong>(key.uidValidity));
    q.bindValue(QStringLiteral(":u"), static_cast<qulonglong>(key.uid));
    if (!q.exec())
    {
        qWarning() << "PersistentMessageCache: delete failed:" << q.lastError().text();
    }
}

void PersistentMessageCache::invalidateMailbox(const QString& mailbox)
{
    if (!enabled_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(writeMutex_);
    auto db = ensureConnection();
    if (!db.isOpen())
    {
        return;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM messages WHERE account_id=:a AND mailbox=:m"));
    q.bindValue(QStringLiteral(":a"), accountId_);
    q.bindValue(QStringLiteral(":m"), mailbox);
    if (!q.exec())
    {
        qWarning() << "PersistentMessageCache: delete-by-mailbox failed:" << q.lastError().text();
    }
}

void PersistentMessageCache::destroy()
{
    if (!enabled_)
    {
        // Even if we never opened the DB, do attempt to remove a stale file.
        if (QFile::exists(filePath_))
        {
            QFile::remove(filePath_);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(writeMutex_);

    // Best-effort overwrite-then-unlink: we can't truly shred contents on modern
    // filesystems with journaling/SSDs, but we can at least drop rows so even a
    // raw-file recovery sees no plaintext mailbox/UID metadata, then unlink.
    auto db = ensureConnection();
    if (db.isOpen())
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("DELETE FROM messages")))
        {
            qWarning() << "PersistentMessageCache: destroy DELETE failed:" << q.lastError().text();
        }
        // VACUUM rewrites the db file without the deleted rows. Best-effort only.
        if (!q.exec(QStringLiteral("VACUUM")))
        {
            qWarning() << "PersistentMessageCache: VACUUM failed:" << q.lastError().text();
        }
        db.close();
    }

    enabled_ = false;

    // Remove all per-thread connections we know how to name. We can only safely
    // remove the one bound to this thread; the rest will be cleaned up at thread
    // exit via Qt's mechanism.
    const QString name = baseConnectionName_ + QStringLiteral("-")
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name))
    {
        QSqlDatabase::removeDatabase(name);
    }

    if (QFile::exists(filePath_))
    {
        QFile::remove(filePath_);
    }
    // SQLite WAL leaves "-wal" and "-shm" sidecar files; remove them too.
    QFile::remove(filePath_ + QStringLiteral("-wal"));
    QFile::remove(filePath_ + QStringLiteral("-shm"));
}

void PersistentMessageCache::flush()
{
    if (!enabled_)
    {
        return;
    }
    auto db = ensureConnection();
    if (!db.isOpen())
    {
        return;
    }
    QSqlQuery q(db);
    // WAL checkpoint pushes pending writes from the WAL into the main DB file.
    if (!q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")))
    {
        // Non-fatal.
    }
}

void PersistentMessageCache::enforceBudget_locked()
{
    auto db = ensureConnection();
    if (!db.isOpen())
    {
        return;
    }

    // Cheap aggregate first; only enter the eviction loop if we are over budget.
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COALESCE(SUM(size_bytes),0) FROM messages")))
    {
        return;
    }
    if (!q.next())
    {
        return;
    }
    const auto total = static_cast<std::uint64_t>(q.value(0).toLongLong());
    if (total <= maxBytes_)
    {
        return;
    }

    std::uint64_t over = total - maxBytes_;

    // Walk oldest-first, deleting until we're back under budget. Batch in chunks
    // of 32 rows to amortise statement overhead.
    while (over > 0)
    {
        QSqlQuery sel(db);
        if (!sel.exec(QStringLiteral(
                "SELECT account_id, mailbox, uid_validity, uid, size_bytes "
                "FROM messages ORDER BY cached_at ASC LIMIT 32")))
        {
            break;
        }

        std::vector<std::tuple<QString, QString, qulonglong, qulonglong>> doomed;
        std::uint64_t freed = 0;
        while (sel.next())
        {
            doomed.emplace_back(
                sel.value(0).toString(),
                sel.value(1).toString(),
                sel.value(2).toULongLong(),
                sel.value(3).toULongLong());
            freed += static_cast<std::uint64_t>(sel.value(4).toLongLong());
        }
        if (doomed.empty())
        {
            break;
        }

        QSqlQuery del(db);
        del.prepare(QStringLiteral(
            "DELETE FROM messages "
            "WHERE account_id=:a AND mailbox=:m AND uid_validity=:v AND uid=:u"));
        for (const auto& [a, m, v, u] : doomed)
        {
            del.bindValue(QStringLiteral(":a"), a);
            del.bindValue(QStringLiteral(":m"), m);
            del.bindValue(QStringLiteral(":v"), v);
            del.bindValue(QStringLiteral(":u"), u);
            if (!del.exec())
            {
                qWarning() << "PersistentMessageCache: eviction delete failed:" << del.lastError().text();
            }
        }

        if (freed >= over)
        {
            break;
        }
        over -= freed;
    }
}

}  // namespace aurora::mail::app::cache
