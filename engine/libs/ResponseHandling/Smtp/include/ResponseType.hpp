#ifndef SMTP_RESPONSE_HPP
#define SMTP_RESPONSE_HPP

#include <EnhancedCode.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace aurora::mail::smtp::response
{

  /**
   * @brief Represents a parsed SMTP response
   *
   * Contains the status code, optional enhanced code, and human-readable text.
   */
  struct SmtpResponse
  {
    uint16_t code{ 0 };                         ///< 3-digit SMTP status code (e.g., 250, 550)
    std::optional<EnhancedCode> enhanced_code;  ///< Optional enhanced status code (RFC 3463)
    std::string text;                           ///< Human-readable message text
    std::string raw_response;                   ///< Original raw response for debugging

    /**
     * @brief Check if response indicates success (2xx)
     */
    bool isSuccess() const
    {
      return code >= 200 && code < 300;
    }

    /**
     * @brief Check if response is a transient failure (4xx)
     */
    bool isTransientFailure() const
    {
      return code >= 400 && code < 500;
    }

    /**
     * @brief Check if response is a permanent failure (5xx)
     */
    bool isPermanentFailure() const
    {
      return code >= 500 && code < 600;
    }

    /**
     * @brief Check if response needs more input (3xx)
     */
    bool needsMoreInput() const
    {
      return code >= 300 && code < 400;
    }
  };

}  // namespace aurora::mail::smtp::response

#endif  // SMTP_RESPONSE_HPP
