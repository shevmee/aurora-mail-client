#include "CacheKeyMaterial.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QString>

#if defined(AURORA_USE_KEYCHAIN) && AURORA_USE_KEYCHAIN
#include "KeychainBackend.hpp"
#endif

namespace aurora::mail::app::cache
{

namespace
{
constexpr const char* kKeyIdPrefix = "msgcache:";
}  // namespace

QString CacheKeyMaterial::keyIdentifier(const QString& accountId)
{
    // Hash the account id so the keychain entry name does not leak the email
    // address verbatim to anyone who can list keychain items (still inevitable
    // on most platforms, but at least we don't leak it again here).
    const QByteArray digest = QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(kKeyIdPrefix) + QString::fromLatin1(digest.toHex().left(32));
}

std::optional<AesGcmCipher::Key> CacheKeyMaterial::loadOrCreate(const QString& accountId)
{
#if defined(AURORA_USE_KEYCHAIN) && AURORA_USE_KEYCHAIN
    if (accountId.isEmpty())
    {
        return std::nullopt;
    }

    KeychainBackend keychain;
    const QString id = keyIdentifier(accountId);
    QString existing = keychain.retrieve(id);

    AesGcmCipher::Key key{};
    if (!existing.isEmpty())
    {
        const QByteArray raw = QByteArray::fromBase64(existing.toLatin1());
        if (raw.size() != static_cast<int>(AesGcmCipher::kKeyBytes))
        {
            qWarning() << "CacheKeyMaterial: stored key for account has unexpected size; regenerating";
        }
        else
        {
            std::memcpy(key.data(), raw.constData(), AesGcmCipher::kKeyBytes);
            return key;
        }
    }

    auto fresh = AesGcmCipher::generateKey();
    if (!fresh.has_value())
    {
        qWarning() << "CacheKeyMaterial: RAND_bytes failed; persistent cache will be disabled";
        return std::nullopt;
    }
    key = *fresh;

    QByteArray raw(reinterpret_cast<const char*>(key.data()), static_cast<int>(key.size()));
    keychain.store(id, QString::fromLatin1(raw.toBase64()));
    // Wipe the temporary base64 buffer.
    raw.fill('\0');
    return key;
#else
    Q_UNUSED(accountId);
    // No secure storage available. Refuse to provision a key — the persistent
    // tier will be disabled and the cache will fall back to memory-only,
    // because writing user mail to disk in the clear is not an option.
    qWarning() << "CacheKeyMaterial: keychain unavailable; persistent message cache disabled";
    return std::nullopt;
#endif
}

void CacheKeyMaterial::destroy(const QString& accountId)
{
#if defined(AURORA_USE_KEYCHAIN) && AURORA_USE_KEYCHAIN
    if (accountId.isEmpty())
    {
        return;
    }
    KeychainBackend keychain;
    keychain.remove(keyIdentifier(accountId));
#else
    Q_UNUSED(accountId);
#endif
}

}  // namespace aurora::mail::app::cache
