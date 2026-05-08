#include "AesGcmCipher.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <QByteArray>

#include <cstring>
#include <memory>
#include <vector>

namespace aurora::mail::app::cache
{

namespace
{

struct CipherCtxDeleter
{
    void operator()(EVP_CIPHER_CTX* ctx) const noexcept
    {
        if (ctx != nullptr)
        {
            EVP_CIPHER_CTX_free(ctx);
        }
    }
};
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter>;

}  // namespace

std::optional<AesGcmCipher::Key> AesGcmCipher::generateKey()
{
    Key key{};
    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1)
    {
        return std::nullopt;
    }
    return key;
}

void AesGcmCipher::secureWipe(Key& key) noexcept
{
    OPENSSL_cleanse(key.data(), key.size());
}

std::optional<QByteArray> AesGcmCipher::seal(const Key& key, const QByteArray& plaintext)
{
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
    {
        return std::nullopt;
    }

    // Use the modern AEAD recommended in the project's crypto rule:
    // EVP_aes_256_gcm() with a fresh 96-bit nonce.
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        return std::nullopt;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceBytes), nullptr) != 1)
    {
        return std::nullopt;
    }

    std::array<std::uint8_t, kNonceBytes> nonce{};
    // Random per-message nonce. NEVER reuse with the same key — GCM's authentication
    // and confidentiality both collapse on nonce reuse.
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1)
    {
        return std::nullopt;
    }

    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1)
    {
        return std::nullopt;
    }

    // Output framing: [nonce][ciphertext][tag]. Reserve maximum needed up front.
    QByteArray out;
    out.resize(static_cast<int>(kNonceBytes + plaintext.size() + kTagBytes));
    auto* outBytes = reinterpret_cast<std::uint8_t*>(out.data());

    std::memcpy(outBytes, nonce.data(), kNonceBytes);

    int ctOffset = static_cast<int>(kNonceBytes);
    int written = 0;

    if (EVP_EncryptUpdate(
            ctx.get(),
            outBytes + ctOffset,
            &written,
            reinterpret_cast<const std::uint8_t*>(plaintext.constData()),
            plaintext.size())
        != 1)
    {
        return std::nullopt;
    }
    int totalCipher = written;

    if (EVP_EncryptFinal_ex(ctx.get(), outBytes + ctOffset + totalCipher, &written) != 1)
    {
        return std::nullopt;
    }
    totalCipher += written;

    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(),
            EVP_CTRL_GCM_GET_TAG,
            static_cast<int>(kTagBytes),
            outBytes + ctOffset + totalCipher)
        != 1)
    {
        return std::nullopt;
    }

    out.resize(ctOffset + totalCipher + static_cast<int>(kTagBytes));
    return out;
}

std::optional<QByteArray> AesGcmCipher::open(const Key& key, const QByteArray& sealed)
{
    if (sealed.size() < static_cast<int>(kNonceBytes + kTagBytes))
    {
        return std::nullopt;
    }

    const auto* sealedBytes = reinterpret_cast<const std::uint8_t*>(sealed.constData());
    const std::uint8_t* nonce = sealedBytes;
    const std::uint8_t* ciphertext = sealedBytes + kNonceBytes;
    const int ciphertextLen = sealed.size() - static_cast<int>(kNonceBytes + kTagBytes);
    const std::uint8_t* tag = sealedBytes + kNonceBytes + ciphertextLen;

    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
    {
        return std::nullopt;
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        return std::nullopt;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceBytes), nullptr) != 1)
    {
        return std::nullopt;
    }
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce) != 1)
    {
        return std::nullopt;
    }

    QByteArray plaintext;
    plaintext.resize(ciphertextLen);  // upper bound; GCM has no padding
    auto* plainBytes = reinterpret_cast<std::uint8_t*>(plaintext.data());

    int written = 0;
    if (EVP_DecryptUpdate(ctx.get(), plainBytes, &written, ciphertext, ciphertextLen) != 1)
    {
        return std::nullopt;
    }
    int totalPlain = written;

    // Set the expected tag BEFORE EVP_DecryptFinal_ex so it can authenticate.
    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(),
            EVP_CTRL_GCM_SET_TAG,
            static_cast<int>(kTagBytes),
            // OpenSSL requires a non-const pointer here even though it is read-only.
            const_cast<std::uint8_t*>(tag))
        != 1)
    {
        return std::nullopt;
    }

    // Returns 1 only if the tag is valid; on failure the partial plaintext is
    // discarded (we never return it).
    if (EVP_DecryptFinal_ex(ctx.get(), plainBytes + totalPlain, &written) != 1)
    {
        return std::nullopt;
    }
    totalPlain += written;

    plaintext.resize(totalPlain);
    return plaintext;
}

}  // namespace aurora::mail::app::cache
