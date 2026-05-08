#ifndef COMMAND_VALIDATION_HPP
#define COMMAND_VALIDATION_HPP

#include <expected>
#include <format>
#include <string_view>

#include "ProtocolError.hpp"

namespace aurora::mail::common
{

  /**
   * @brief Reject CR/LF in user-supplied SMTP/IMAP command fields.
   *
   * Both SMTP and IMAP are line-oriented: a stray "\r\n" in any caller-controlled
   * field would be smuggled as an additional command (CRLF injection). All
   * parameterized command structs must call this on every user-supplied field
   * before composing wire bytes.
   *
   * Returns std::expected<void, ProtocolError> so the failure threads through
   * the existing Result<T> pipeline without exceptions.
   */
  [[nodiscard]] inline std::expected<void, ProtocolError> validateNoCrlf(
      std::string_view value,
      std::string_view field_name) noexcept(false)
  {
    for (std::size_t i = 0; i < value.size(); ++i)
    {
      const char c = value[i];
      if (c == '\r' || c == '\n' || c == '\0')
      {
        return std::unexpected(
            ProtocolError::protocol(
                std::format("Invalid character in command field '{}' at offset {}", field_name, i),
                "CR/LF/NUL bytes are forbidden in command parameters (CRLF injection guard)"));
      }
    }
    return {};
  }

}  // namespace aurora::mail::common

#endif  // COMMAND_VALIDATION_HPP
