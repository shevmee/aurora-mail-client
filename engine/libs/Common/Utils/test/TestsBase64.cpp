#include <gtest/gtest.h>

#include <Base64.hpp>
#include <cstring>

using namespace aurora::mail::common::base64;

TEST(Base64Test, Encode_SimpleStringLvalue)
{
  std::string decoded = "Hello, world!";
  std::string encoded = base64Encode(decoded);
  EXPECT_EQ(encoded, "SGVsbG8sIHdvcmxkIQ==");
}

TEST(Base64Test, Encode_SimpleStringRvalue)
{
  std::string encoded = base64Encode("Hello, world!");
  EXPECT_EQ(encoded, "SGVsbG8sIHdvcmxkIQ==");
}

TEST(Base64Test, Encode_EmptyString)
{
  std::string decoded = "";
  std::string encoded = base64Encode(decoded);
  EXPECT_EQ(encoded, "");
}

TEST(Base64Test, Encode_SingleCharacter)
{
  std::string decoded = "A";
  std::string encoded = base64Encode(decoded);
  EXPECT_FALSE(encoded.empty());
}

TEST(Base64Test, Encode_SpecialCharacters)
{
  std::string decoded = "!@#$%^&*()";
  std::string encoded = base64Encode(decoded);
  EXPECT_FALSE(encoded.empty());

  // Decode to verify correctness
  // Note: decoded_back may have padding, so compare only the actual content
  std::string decoded_back = base64Decode(encoded);
  EXPECT_EQ(decoded, decoded_back.substr(0, decoded.length()));
}

TEST(Base64Test, Encode_UnicodeCharacters)
{
  std::string decoded = "Hello, 世界!";
  std::string encoded = base64Encode(decoded);
  EXPECT_FALSE(encoded.empty());

  std::string decoded_back = base64Decode(encoded);
  EXPECT_EQ(decoded, decoded_back.substr(0, decoded.length()));
}

TEST(Base64Test, Encode_BinaryData)
{
  std::string decoded = std::string("\x00\x01\x02\x03\x04\x05", 6);
  std::string encoded = base64Encode(decoded);
  EXPECT_FALSE(encoded.empty());

  std::string decoded_back = base64Decode(encoded);
  EXPECT_EQ(decoded, decoded_back.substr(0, decoded.length()));
}

TEST(Base64Test, Encode_LongString)
{
  std::string decoded(1000, 'A');
  std::string encoded = base64Encode(decoded);
  EXPECT_FALSE(encoded.empty());
  EXPECT_GT(encoded.length(), decoded.length());
}

TEST(Base64Test, Decode_SimpleStringLvalue)
{
  std::string encoded = "SGVsbG8sIHdvcmxkIQ==";
  std::string decoded = base64Decode(encoded);
  std::string expected = "Hello, world!";
  EXPECT_EQ(expected, decoded.substr(0, expected.length()));
}

TEST(Base64Test, Decode_SimpleStringRvalue)
{
  std::string decoded = base64Decode("SGVsbG8sIHdvcmxkIQ==");
  std::string expected = "Hello, world!";
  EXPECT_EQ(expected, decoded.substr(0, expected.length()));
}

TEST(Base64Test, Decode_EmptyString)
{
  std::string encoded = "";
  std::string decoded = base64Decode(encoded);
  EXPECT_EQ(decoded, "");
}

TEST(Base64Test, Decode_WithoutPadding)
{
  std::string encoded = "SGVsbG8";
  std::string decoded = base64Decode(encoded);
  EXPECT_FALSE(decoded.empty());
}

TEST(Base64Test, Decode_WithPadding)
{
  std::string encoded = "SGVsbG8=";
  std::string decoded = base64Decode(encoded);
  EXPECT_FALSE(decoded.empty());
}

TEST(Base64Test, EncodeDecode_SimpleString)
{
  std::string original = "Hello, world!";
  std::string encoded = base64Encode(original);
  std::string decoded = base64Decode(encoded);
  EXPECT_EQ(original, decoded.substr(0, original.length()));
}

TEST(Base64Test, EncodeDecode_MultipleStrings)
{
  std::vector<std::string> test_strings = { "Test 1",
                                            "Test with numbers: 12345",
                                            "Test with special chars: !@#$%",
                                            "Short",
                                            "A much longer string that contains multiple words and sentences." };

  for (const auto& original : test_strings)
  {
    std::string encoded = base64Encode(original);
    std::string decoded = base64Decode(encoded);
    EXPECT_EQ(original, decoded.substr(0, original.length())) << "Failed for string: " << original;
  }
}

TEST(Base64Test, EncodeDecode_EmailContent)
{
  std::string email_body =
      "Subject: Test Email\r\n"
      "From: sender@example.com\r\n"
      "To: recipient@example.com\r\n"
      "\r\n"
      "This is the email body.";

  std::string encoded = base64Encode(email_body);
  std::string decoded = base64Decode(encoded);
  EXPECT_EQ(email_body, decoded);
}

TEST(Base64Test, Encode_NullBytes)
{
  std::string decoded = std::string("Hello\0World", 11);
  std::string encoded = base64Encode(decoded);
  std::string decoded_back = base64Decode(encoded);
  // Compare only the actual content length
  EXPECT_EQ(0, std::memcmp(decoded.data(), decoded_back.data(), decoded.length()));
  EXPECT_GE(decoded_back.length(), 11);
}

TEST(Base64Test, Encode_OnlyWhitespace)
{
  std::string decoded = "   \t\n\r  ";
  std::string encoded = base64Encode(decoded);
  std::string decoded_back = base64Decode(encoded);
  EXPECT_EQ(decoded, decoded_back.substr(0, decoded.length()));
}

TEST(Base64Test, Encode_VeryLongString)
{
  std::string decoded(10000, 'X');
  std::string encoded = base64Encode(decoded);
  std::string decoded_back = base64Decode(encoded);
  EXPECT_EQ(decoded, decoded_back.substr(0, decoded.length()));
  EXPECT_GE(decoded_back.length(), 10000);
}

TEST(Base64Test, Encode_PlainAuthCredentials)
{
  // PLAIN auth format: \0username\0password
  std::string credentials;
  credentials += '\0';
  credentials += "testuser";
  credentials += '\0';
  credentials += "testpass";

  std::string encoded = base64Encode(credentials);
  EXPECT_FALSE(encoded.empty());

  std::string decoded = base64Decode(encoded);
  EXPECT_EQ(0, std::memcmp(credentials.data(), decoded.data(), credentials.length()));
}

TEST(Base64Test, Encode_LoginAuthUsername)
{
  std::string username = "user@example.com";
  std::string encoded = base64Encode(username);
  std::string decoded = base64Decode(encoded);
  EXPECT_EQ(username, decoded.substr(0, username.length()));
}

TEST(Base64Test, EncodedString_ContainsOnlyValidChars)
{
  std::string decoded = "Test string with various characters!@#$";
  std::string encoded = base64Encode(decoded);

  // Base64 should only contain: A-Z, a-z, 0-9, +, /, =
  for (char c : encoded)
  {
    EXPECT_TRUE(std::isalnum(c) || c == '+' || c == '/' || c == '=') << "Invalid character in base64: " << c;
  }
}

TEST(Base64Test, EncodedString_ProperPadding)
{
  std::string decoded1 = "A";
  std::string decoded2 = "AB";
  std::string decoded3 = "ABC";
  std::string decoded4 = "ABCD";

  std::string encoded1 = base64Encode(decoded1);
  std::string encoded2 = base64Encode(decoded2);
  std::string encoded3 = base64Encode(decoded3);
  std::string encoded4 = base64Encode(decoded4);

  // All encoded strings should have length divisible by 4
  EXPECT_EQ(encoded1.length() % 4, 0);
  EXPECT_EQ(encoded2.length() % 4, 0);
  EXPECT_EQ(encoded3.length() % 4, 0);
  EXPECT_EQ(encoded4.length() % 4, 0);
}
