#ifndef MIME_PARSE_ERROR_HPP
#define MIME_PARSE_ERROR_HPP

#include <cstdint>
#include <string>

namespace aurora::mail::common::mime
{

  /**
   * @brief Error type for MIME parsing failures.
   */
  struct MimeParseError
  {
    enum class Type : uint8_t
    {
      InvalidFormat,        ///< Message doesn't conform to MIME/RFC 5322
      EncodingError,        ///< Failed to decode content
      CharsetError,         ///< Failed to convert charset
      PartExtractionError,  ///< Failed to extract MIME part
      EmptyMessage          ///< Empty or null input
    };

    Type type;
    std::string message;

    MimeParseError(Type t, std::string msg);

    [[nodiscard]] std::string toString() const;
  };

}  // namespace aurora::mail::common::mime

#endif  // MIME_PARSE_ERROR_HPP
