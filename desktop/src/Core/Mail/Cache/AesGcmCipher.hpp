#ifndef AURORA_MAIL_CACHE_AES_GCM_CIPHER_HPP
#define AURORA_MAIL_CACHE_AES_GCM_CIPHER_HPP

#include <QByteArray>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace aurora::mail::app::cache
{

/**
 * AES-256-GCM AEAD wrapper around OpenSSL EVP.
 *
 * - Key:   32 bytes (256-bit). The cache derives it once per account from a
 *          per-account 32-byte master key held in the OS keychain (or, if no
 *          keychain is available, from a randomly generated key file with mode
 *          0600 under app data; the persistent tier degrades to disabled if even
 *          that fails).
 * - Nonce: 12 bytes (96-bit), drawn from a CSPRNG for every encryption. NEVER
 *          reuse a (key, nonce) pair — that breaks GCM catastrophically.
 * - Tag:   16 bytes (128-bit), authenticates the ciphertext. Verified on decrypt.
 *
 * Output framing for encryptionOnly callers: [12-byte nonce][ciphertext][16-byte tag].
 */
class AesGcmCipher
{
public:
    static constexpr std::size_t kKeyBytes = 32;
    static constexpr std::size_t kNonceBytes = 12;
    static constexpr std::size_t kTagBytes = 16;

    using Key = std::array<std::uint8_t, kKeyBytes>;

    /**
     * Encrypt `plaintext` under `key`. Generates a fresh random nonce per call.
     *
     * Returns nonce || ciphertext || tag, suitable for storage as a single blob.
     * Returns std::nullopt only on programming or OpenSSL errors (never on bad
     * inputs — encryption cannot fail on well-formed inputs).
     */
    [[nodiscard]] static std::optional<QByteArray> seal(const Key& key, const QByteArray& plaintext);

    /**
     * Decrypt and authenticate a blob produced by seal().
     *
     * Returns std::nullopt on any authentication failure, framing error, or
     * OpenSSL failure. CRITICAL: never accept the plaintext if the tag check
     * fails; AEAD provides no integrity otherwise.
     */
    [[nodiscard]] static std::optional<QByteArray> open(const Key& key, const QByteArray& sealed);

    /**
     * Generate a fresh 32-byte key from a CSPRNG. Used when provisioning a new
     * per-account master key.
     */
    [[nodiscard]] static std::optional<Key> generateKey();

    /// Constant-time wipe of a key buffer.
    static void secureWipe(Key& key) noexcept;
};

}  // namespace aurora::mail::app::cache

#endif  // AURORA_MAIL_CACHE_AES_GCM_CIPHER_HPP
