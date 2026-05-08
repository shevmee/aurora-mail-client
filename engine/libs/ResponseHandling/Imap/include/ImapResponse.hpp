#ifndef IMAP_RESPONSE_HPP
#define IMAP_RESPONSE_HPP

#include <ImapValue.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::mail::imap::response
{

  /**
   * @brief Represents the category of an IMAP status code.
   */
  enum class StatusType : std::uint8_t
  {
    OK,        ///< Command completed successfully.
    NO,        ///< Command failed.
    BAD,       ///< Protocol-level error.
    BYE,       ///< Server is closing the connection.
    PREAUTH,   ///< Pre-authenticated connection (greeting only).
    Undefined  ///< Response doesn't match known status types.
  };

  // Helper: Convert status string to StatusType
  [[nodiscard]] constexpr StatusType stringToStatusType(std::string_view status_str) noexcept
  {
    if (status_str == "OK")
      return StatusType::OK;
    if (status_str == "NO")
      return StatusType::NO;
    if (status_str == "BAD")
      return StatusType::BAD;
    if (status_str == "BYE")
      return StatusType::BYE;
    if (status_str == "PREAUTH")
      return StatusType::PREAUTH;
    return StatusType::Undefined;
  }

  /**
   * @brief Convert StatusType to string representation
   */
  [[nodiscard]] constexpr std::string_view statusTypeToString(StatusType status) noexcept
  {
    switch (status)
    {
      case StatusType::OK: return "OK";
      case StatusType::NO: return "NO";
      case StatusType::BAD: return "BAD";
      case StatusType::BYE: return "BYE";
      case StatusType::PREAUTH: return "PREAUTH";
      case StatusType::Undefined: return "Undefined";
      default: return "Unknown";
    }
  }

  /**
   * @brief Represents a single untagged IMAP response line
   */
  struct UntaggedResponse
  {
    std::string_view line;                     ///< Full untagged response line
    std::string_view command;                  ///< Command type (FETCH, EXISTS, etc.)
    std::string_view data;                     ///< Response data/payload
    std::vector<parser::Value> parsed_values;  ///< Tokenized/structured response data

    UntaggedResponse() = default;
    constexpr UntaggedResponse(std::string_view l, std::string_view cmd, std::string_view d) noexcept
        : line(l),
          command(cmd),
          data(d)
    {
    }
  };

  /**
   * @brief Parses and represents an IMAP response
   */
  struct ImapResponse
  {
    std::string_view tag;                        ///< Response tag (e.g., 'A001')
    StatusType status{ StatusType::Undefined };  ///< Status: OK, NO, BAD, BYE
    std::string_view text;                       ///< Status text/message
    std::vector<UntaggedResponse> untagged;      ///< Untagged responses containing data
    std::string raw_response;                    ///< Complete raw response from server

    [[nodiscard]] constexpr bool isSuccess() const noexcept
    {
      return status == StatusType::OK;
    }

    [[nodiscard]] constexpr bool hasUntagged() const noexcept
    {
      return !untagged.empty();
    }
  };

}  // namespace aurora::mail::imap::response

#endif  // IMAP_RESPONSE_HPP
