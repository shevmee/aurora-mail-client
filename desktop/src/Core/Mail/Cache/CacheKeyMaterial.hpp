#ifndef AURORA_MAIL_CACHE_CACHE_KEY_MATERIAL_HPP
#define AURORA_MAIL_CACHE_CACHE_KEY_MATERIAL_HPP

#include "AesGcmCipher.hpp"

#include <QString>

#include <optional>

namespace aurora::mail::app::cache
{

/**
 * Provisioning of per-account 256-bit master keys for the persistent cache.
 *
 * Strategy, in order of preference:
 *   1. Store the key in the OS keychain via the existing AuroraMail keychain
 *      backend (macOS Keychain / Windows Credential Store / libsecret/KWallet).
 *      This is the only configuration that satisfies the project's "no plaintext
 *      mail at rest" stance.
 *   2. If the keychain is not built in (USE_SYSTEM_KEYCHAIN=OFF, or QtKeychain
 *      missing), refuse to provision a key and return std::nullopt — the
 *      persistent tier will then disable itself rather than write plaintext.
 *
 * Key identifiers are namespaced under "aurora-mail.msgcache." so they cannot
 * collide with OAuth or password credentials stored under the same service name.
 */
class CacheKeyMaterial
{
public:
    /**
     * Load (or create) the master key for `accountId`.
     *
     * @return The 32-byte AES-256-GCM key, or std::nullopt if the platform has no
     *         secure storage available (in which case the persistent cache MUST
     *         disable itself instead of falling back to a plaintext file).
     */
    [[nodiscard]] static std::optional<AesGcmCipher::Key> loadOrCreate(const QString& accountId);

    /**
     * Permanently remove the master key for `accountId`.
     * Called on sign-out / account removal so the on-disk ciphertext becomes
     * unrecoverable even before the file itself is unlinked.
     */
    static void destroy(const QString& accountId);

private:
    static QString keyIdentifier(const QString& accountId);
};

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_CACHE_KEY_MATERIAL_HPP
