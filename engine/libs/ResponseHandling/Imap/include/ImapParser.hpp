#ifndef IMAP_PARSER_HPP
#define IMAP_PARSER_HPP

#include <ImapResponse.hpp>
#include <expected>
#include <string_view>

namespace aurora::mail::imap::response
{

  /**
   * @brief Parse IMAP response from raw string
   *
   * Supports tagged/untagged responses, literals, and lists.
   *
   * @param raw_response Raw IMAP response text
   * @param is_greeting If true, validates that response is a valid greeting
   * (untagged OK/PREAUTH/BYE)
   * @return std::expected<ImapResponse, std::string> Parsed response or error
   * message
   *
   * @example
   *   // Regular response
   *   auto result = imap::response::parse("* 5 EXISTS\r\nA001 OK Success\r\n");
   *   // Greeting
   *   auto greeting = imap::response::parse("* OK IMAP4rev1 Service Ready\r\n",
   * true);
   */
  std::expected<ImapResponse, std::string> parse(std::string_view raw_response, bool is_greeting = false);

  /**
   * @brief Check if a line represents a complete IMAP greeting
   *
   * IMAP greetings are untagged responses that start with:
   * - "* OK" (server ready)
   * - "* PREAUTH" (pre-authenticated connection)
   * - "* BYE" (server refusing connection)
   *
   * @param line The line to check
   * @return true if the line is a complete greeting, false otherwise
   */
  [[nodiscard]] bool isGreetingLine(std::string_view line) noexcept;

}  // namespace aurora::mail::imap::response

#endif  // IMAP_PARSER_HPP
