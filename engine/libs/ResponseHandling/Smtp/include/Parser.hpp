#ifndef SMTP_PARSER_HPP
#define SMTP_PARSER_HPP

#include <ResponseType.hpp>
#include <expected>
#include <string>

namespace aurora::mail::smtp::response
{

  /**
   * @brief Parse SMTP response from raw string
   *
   * Supports multi-line responses and enhanced status codes.
   *
   * @param raw_response Raw SMTP response text (may contain CRLF)
   * @return std::expected<SmtpResponse, std::string> Parsed response or error
   * message
   *
   * @example
   *   auto result = smtp::response::parse("250 2.0.0 OK\r\n");
   *   if (result) {
   *       std::cout << "Code: " << result->code << "\n";
   *       if (result->enhanced_code) {
   *           std::cout << "Enhanced: " << result->enhanced_code->toString() <<
   * "\n";
   *       }
   *   }
   */
  std::expected<SmtpResponse, std::string> parse(const std::string& raw_response);

}  // namespace aurora::mail::smtp::response

#endif  // SMTP_PARSER_HPP
