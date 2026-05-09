#include <gtest/gtest.h>

#include <Mail/Cache/AesGcmCipher.hpp>
#include <QByteArray>
#include <array>

using aurora::mail::app::cache::AesGcmCipher;

namespace
{
  AesGcmCipher::Key zeroKey()
  {
    AesGcmCipher::Key k{};
    k.fill(0);
    return k;
  }

  AesGcmCipher::Key fixedKey(std::uint8_t fill)
  {
    AesGcmCipher::Key k{};
    k.fill(fill);
    return k;
  }
}  // namespace

// ---------------------------------------------------------------------------
// Basic AEAD contract
// ---------------------------------------------------------------------------

TEST(AesGcmCipher, SealOpenRoundTrip)
{
  auto k = fixedKey(0x42);
  const QByteArray pt("hello world", 11);
  auto sealed = AesGcmCipher::seal(k, pt);
  ASSERT_TRUE(sealed.has_value());
  EXPECT_GE(sealed->size(), static_cast<int>(AesGcmCipher::kNonceBytes + AesGcmCipher::kTagBytes))
      << "sealed blob must include a 12-byte nonce and a 16-byte tag";

  auto opened = AesGcmCipher::open(k, *sealed);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(*opened, pt);
}

TEST(AesGcmCipher, RoundTripPreservesEmptyPlaintext)
{
  auto k = fixedKey(0x11);
  auto sealed = AesGcmCipher::seal(k, QByteArray());
  ASSERT_TRUE(sealed.has_value());
  // nonce + tag = 28 bytes minimum, even for empty plaintext.
  EXPECT_EQ(sealed->size(), static_cast<int>(AesGcmCipher::kNonceBytes + AesGcmCipher::kTagBytes));

  auto opened = AesGcmCipher::open(k, *sealed);
  ASSERT_TRUE(opened.has_value());
  EXPECT_TRUE(opened->isEmpty());
}

TEST(AesGcmCipher, RoundTripBinaryPlaintext)
{
  auto k = fixedKey(0x77);
  QByteArray pt;
  pt.reserve(1024);
  for (int i = 0; i < 1024; ++i)
    pt.append(static_cast<char>(i & 0xFF));

  auto sealed = AesGcmCipher::seal(k, pt);
  ASSERT_TRUE(sealed.has_value());
  auto opened = AesGcmCipher::open(k, *sealed);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(*opened, pt);
}

TEST(AesGcmCipher, NonceIsRandomPerCall)
{
  // GCM is catastrophically broken on (key, nonce) reuse. Verify that two
  // back-to-back seals with the same (key, plaintext) produce distinct
  // nonces (and therefore distinct ciphertexts).
  auto k = fixedKey(0x55);
  const QByteArray pt("aaaaaaaaaaaaaaaaaaaa", 20);

  auto a = AesGcmCipher::seal(k, pt);
  auto b = AesGcmCipher::seal(k, pt);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(*a, *b) << "AES-GCM nonce reuse is a critical security bug. "
                       "Two independent seals of the same plaintext must "
                       "differ in their (random) nonce.";

  // Both must still decrypt back to the same plaintext under the same key.
  auto opa = AesGcmCipher::open(k, *a);
  auto opb = AesGcmCipher::open(k, *b);
  ASSERT_TRUE(opa.has_value());
  ASSERT_TRUE(opb.has_value());
  EXPECT_EQ(*opa, pt);
  EXPECT_EQ(*opb, pt);
}

// ---------------------------------------------------------------------------
// Authentication / tamper resistance
// ---------------------------------------------------------------------------

TEST(AesGcmCipher, OpenWithDifferentKeyFails)
{
  auto sealed = AesGcmCipher::seal(fixedKey(0xAA), QByteArray("payload", 7));
  ASSERT_TRUE(sealed.has_value());
  EXPECT_FALSE(AesGcmCipher::open(fixedKey(0xBB), *sealed).has_value());
}

TEST(AesGcmCipher, OpenWithFlippedCiphertextByteFails)
{
  auto k = fixedKey(0x33);
  auto sealed = AesGcmCipher::seal(k, QByteArray("important", 9));
  ASSERT_TRUE(sealed.has_value());
  // Flip a byte inside the ciphertext (just past the 12-byte nonce).
  ASSERT_GT(sealed->size(), static_cast<int>(AesGcmCipher::kNonceBytes));
  (*sealed)[AesGcmCipher::kNonceBytes] ^= char(0x01);
  EXPECT_FALSE(AesGcmCipher::open(k, *sealed).has_value()) << "AEAD must refuse a ciphertext whose tag does not match.";
}

TEST(AesGcmCipher, OpenWithFlippedTagByteFails)
{
  auto k = fixedKey(0x44);
  auto sealed = AesGcmCipher::seal(k, QByteArray("important", 9));
  ASSERT_TRUE(sealed.has_value());
  // Flip the very last byte (inside the 16-byte tag).
  ASSERT_GT(sealed->size(), 0);
  (*sealed)[sealed->size() - 1] ^= char(0x80);
  EXPECT_FALSE(AesGcmCipher::open(k, *sealed).has_value());
}

TEST(AesGcmCipher, OpenWithFlippedNonceByteFails)
{
  auto k = fixedKey(0x77);
  auto sealed = AesGcmCipher::seal(k, QByteArray("important", 9));
  ASSERT_TRUE(sealed.has_value());
  ASSERT_GT(sealed->size(), 0);
  // Flip a nonce byte. The auth tag was computed over the original nonce,
  // so the verification must fail.
  (*sealed)[0] ^= char(0x10);
  EXPECT_FALSE(AesGcmCipher::open(k, *sealed).has_value());
}

TEST(AesGcmCipher, OpenRejectsBlobShorterThanNoncePlusTag)
{
  // 12-byte nonce + 16-byte tag = 28 bytes minimum framing.
  EXPECT_FALSE(AesGcmCipher::open(fixedKey(0x00), QByteArray()).has_value());
  EXPECT_FALSE(AesGcmCipher::open(fixedKey(0x00), QByteArray(10, '\0')).has_value());
  EXPECT_FALSE(AesGcmCipher::open(fixedKey(0x00), QByteArray(27, '\0')).has_value());
}

TEST(AesGcmCipher, OpenWithZeroBytesAtRequiredFramingSize)
{
  // Exactly 28 bytes of zeros is well-formed in size but the tag is just
  // zeros, which will not authenticate against any real ciphertext.
  EXPECT_FALSE(
      AesGcmCipher::open(zeroKey(), QByteArray(static_cast<int>(AesGcmCipher::kNonceBytes + AesGcmCipher::kTagBytes), '\0'))
          .has_value());
}

// ---------------------------------------------------------------------------
// generateKey  /  secureWipe
// ---------------------------------------------------------------------------

TEST(AesGcmCipher, GenerateKeyProducesDistinctKeys)
{
  auto a = AesGcmCipher::generateKey();
  auto b = AesGcmCipher::generateKey();
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(*a, *b) << "Two consecutive generateKey() calls must (with overwhelming probability) "
                       "produce different keys; identical results indicate a broken CSPRNG.";
}

TEST(AesGcmCipher, GeneratedKeyIsNotAllZeros)
{
  auto a = AesGcmCipher::generateKey();
  ASSERT_TRUE(a.has_value());
  AesGcmCipher::Key zero{};
  zero.fill(0);
  EXPECT_NE(*a, zero) << "Generated key must not be all-zero (would indicate "
                         "an uninitialised buffer rather than CSPRNG output).";
}

TEST(AesGcmCipher, SecureWipeZeroesEveryByte)
{
  auto k = fixedKey(0xFF);
  AesGcmCipher::secureWipe(k);
  for (auto b : k)
  {
    EXPECT_EQ(b, 0u);
  }
}

TEST(AesGcmCipher, GeneratedKeyDecryptsItsOwnSeal)
{
  auto k = AesGcmCipher::generateKey();
  ASSERT_TRUE(k.has_value());
  const QByteArray pt("end-to-end test", 15);
  auto sealed = AesGcmCipher::seal(*k, pt);
  ASSERT_TRUE(sealed.has_value());
  auto opened = AesGcmCipher::open(*k, *sealed);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(*opened, pt);
}
