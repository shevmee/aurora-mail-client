#include <Base64.hpp>
#include <ImapUtf7.hpp>
#include <boost/locale.hpp>
#include <string>

namespace aurora::mail::common::utils
{

  // Helper: Convert UTF-16BE bytes to UTF-8
  static std::string utf16beToUtf8(const std::string& utf16be)
  {
    return boost::locale::conv::between(utf16be, "UTF-8", "UTF-16BE");
  }

  // Helper: Convert UTF-8 to UTF-16BE bytes
  static std::string utf8ToUtf16be(const std::string& utf8)
  {
    return boost::locale::conv::between(utf8, "UTF-16BE", "UTF-8");
  }

  std::string decodeImapUtf7(const std::string& imap_utf7)
  {
    std::string result;
    size_t i = 0;

    while (i < imap_utf7.length())
    {
      if (imap_utf7[i] == '&')
      {
        // Find the end of the encoded section (marked by -)
        size_t end = imap_utf7.find('-', i + 1);
        if (end == std::string::npos)
        {
          // Malformed, treat as literal
          result += '&';
          i++;
          continue;
        }

        // Special case: &- represents literal &
        if (end == i + 1)
        {
          result += '&';
          i = end + 1;
          continue;
        }

        // Extract the encoded section
        std::string encoded = imap_utf7.substr(i + 1, end - i - 1);

        // Convert IMAP Modified Base64 to standard Base64
        // IMAP uses , instead of /
        for (char& c : encoded)
        {
          if (c == ',')
            c = '/';
        }

        // Add padding if needed (IMAP doesn't use padding)
        size_t padding = (4 - (encoded.length() % 4)) % 4;
        encoded.append(padding, '=');

        // Decode base64
        std::string decoded = base64::base64Decode(encoded);

        // Convert UTF-16BE to UTF-8
        result += utf16beToUtf8(decoded);

        i = end + 1;
      }
      else
      {
        // Regular ASCII character
        result += imap_utf7[i];
        i++;
      }
    }

    return result;
  }

  std::string encodeImapUtf7(const std::string& utf8)
  {
    std::string result;
    size_t i = 0;

    while (i < utf8.length())
    {
      unsigned char c = utf8[i];

      // ASCII printable characters except &
      if (c >= 0x20 && c <= 0x7E && c != '&')
      {
        result += c;
        i++;
      }
      else if (c == '&')
      {
        result += "&-";
        i++;
      }
      else
      {
        // Non-ASCII: collect consecutive non-ASCII chars and encode
        size_t start = i;
        while (i < utf8.length())
        {
          unsigned char ch = utf8[i];
          if (ch >= 0x20 && ch <= 0x7E && ch != '&')
          {
            break;
          }
          if (ch == '&')
          {
            break;
          }
          // Handle multi-byte UTF-8
          if ((ch & 0x80) != 0)
          {
            if ((ch & 0xE0) == 0xC0)
              i += 2;
            else if ((ch & 0xF0) == 0xE0)
              i += 3;
            else if ((ch & 0xF8) == 0xF0)
              i += 4;
            else
              i++;
          }
          else
          {
            i++;
          }
        }

        std::string non_ascii = utf8.substr(start, i - start);

        // Convert UTF-8 to UTF-16BE
        std::string utf16be = utf8ToUtf16be(non_ascii);

        // Encode as base64
        std::string base64_encoded = base64::base64Encode(utf16be);

        // Remove padding
        while (!base64_encoded.empty() && base64_encoded.back() == '=')
        {
          base64_encoded.pop_back();
        }

        // Replace / with , (IMAP Modified Base64)
        for (char& ch : base64_encoded)
        {
          if (ch == '/')
            ch = ',';
        }

        result += '&';
        result += base64_encoded;
        result += '-';
      }
    }

    return result;
  }

}  // namespace aurora::mail::common::utils
