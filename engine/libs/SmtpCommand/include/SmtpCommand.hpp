#ifndef SMTP_COMMAND_HPP
#define SMTP_COMMAND_HPP

#include <CommandValidation.hpp>
#include <ProtocolConcepts.hpp>
#include <ProtocolError.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include "Base64.hpp"

namespace aurora::mail::smtp::command
{

  using aurora::mail::common::ProtocolError;
  using aurora::mail::common::Result;
  using aurora::mail::common::validateNoCrlf;

  namespace detail
  {
    inline std::string makeFixed(std::string_view literal)
    {
      return std::string{ literal };
    }

    template<typename... Parts>
    inline std::string concat(std::size_t reserve_hint, Parts&&... parts)
    {
      std::string out;
      out.reserve(reserve_hint);
      (out.append(std::forward<Parts>(parts)), ...);
      return out;
    }
  }  // namespace detail

  struct Helo
  {
    std::string domain;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(domain, "HELO domain"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(7 + domain.size(), "HELO ", domain, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "HELO";
    }
  };

  struct Ehlo
  {
    std::string domain;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(domain, "EHLO domain"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(7 + domain.size(), "EHLO ", domain, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "EHLO";
    }
  };

  struct MailFrom
  {
    std::string address;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(address, "MAIL FROM address"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(13 + address.size(), "MAIL FROM:<", address, ">\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "MAIL FROM";
    }
  };

  struct RcptTo
  {
    std::string address;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(address, "RCPT TO address"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(11 + address.size(), "RCPT TO:<", address, ">\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "RCPT TO";
    }
  };

  struct Data
  {
    [[nodiscard]] Result<std::string> serialize() const
    {
      return detail::makeFixed("DATA\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "DATA";
    }
  };

  struct Quit
  {
    [[nodiscard]] Result<std::string> serialize() const
    {
      return detail::makeFixed("QUIT\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "QUIT";
    }
  };

  struct Rset
  {
    [[nodiscard]] Result<std::string> serialize() const
    {
      return detail::makeFixed("RSET\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "RSET";
    }
  };

  struct StartTls
  {
    [[nodiscard]] Result<std::string> serialize() const
    {
      return detail::makeFixed("STARTTLS\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "STARTTLS";
    }
  };

  struct Noop
  {
    std::string arg;  // empty == no argument

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (arg.empty())
      {
        return detail::makeFixed("NOOP\r\n");
      }
      if (auto v = validateNoCrlf(arg, "NOOP arg"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(7 + arg.size(), "NOOP ", arg, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "NOOP";
    }
  };

  struct Vrfy
  {
    std::string address;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(address, "VRFY address"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(7 + address.size(), "VRFY ", address, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "VRFY";
    }
  };

  struct Help
  {
    std::string arg;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (arg.empty())
      {
        return detail::makeFixed("HELP\r\n");
      }
      if (auto v = validateNoCrlf(arg, "HELP arg"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::concat(7 + arg.size(), "HELP ", arg, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "HELP";
    }
  };

  /**
   * @brief AUTH PLAIN: a single-line command carrying a base64 SASL payload.
   */
  struct AuthPlain
  {
    std::string username;
    std::string password;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(username, "AUTH PLAIN username"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(password, "AUTH PLAIN password"); !v)
      {
        return std::unexpected(v.error());
      }
      // SASL PLAIN: \0username\0password
      std::string payload;
      payload.reserve(2 + username.size() + password.size());
      payload += '\0';
      payload += username;
      payload += '\0';
      payload += password;
      std::string encoded = aurora::mail::common::base64::base64Encode(payload);
      return detail::concat(13 + encoded.size(), "AUTH PLAIN ", encoded, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "AUTH PLAIN";
    }
  };

  /**
   * @brief AUTH LOGIN credentials: drives the multi-step LOGIN handshake.
   *
   * Unlike AuthPlain, the wire form of LOGIN is a 3-step exchange (initial
   * "AUTH LOGIN", then base64(username), then base64(password)). serialize()
   * here returns ONLY the initial command line; SmtpClient::asyncAuthenticate
   * has a dedicated overload that performs the full handshake using these
   * credentials.
   */
  struct AuthLogin
  {
    std::string username;
    std::string password;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(username, "AUTH LOGIN username"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(password, "AUTH LOGIN password"); !v)
      {
        return std::unexpected(v.error());
      }
      return detail::makeFixed("AUTH LOGIN\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "AUTH LOGIN";
    }
  };

  /**
   * @brief AUTH XOAUTH2: a single-line command carrying the base64 SASL IR.
   *
   * Token acquisition is the responsibility of the caller (typically the UI
   * layer with refresh-aware flows). The command is a pure value object: hand
   * it the username and an already-acquired access token.
   *
   * The wire shape is RFC 7628 / Google XOAUTH2:
   *   AUTH XOAUTH2 <base64("user=USER\1auth=Bearer TOKEN\1\1")>
   *
   * Note: many servers may still reply with a "+" continuation containing a
   * base64-encoded error blob; that handshake is handled by SmtpClient.
   */
  struct AuthXOAuth2
  {
    std::string username;
    std::string access_token;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = validateNoCrlf(username, "AUTH XOAUTH2 username"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(access_token, "AUTH XOAUTH2 access_token"); !v)
      {
        return std::unexpected(v.error());
      }
      if (access_token.empty())
      {
        return std::unexpected(
            ProtocolError::auth(
                "AUTH XOAUTH2: access token is empty", "Acquire a valid token before constructing AuthXOAuth2"));
      }
      std::string sasl;
      sasl.reserve(20 + username.size() + access_token.size());
      sasl += "user=";
      sasl += username;
      sasl += "\1auth=Bearer ";
      sasl += access_token;
      sasl += "\1\1";
      std::string encoded = aurora::mail::common::base64::base64Encode(sasl);
      return detail::concat(15 + encoded.size(), "AUTH XOAUTH2 ", encoded, "\r\n");
    }
    static constexpr std::string_view name() noexcept
    {
      return "AUTH XOAUTH2";
    }
  };

  using AuthVariant = std::variant<AuthPlain, AuthLogin, AuthXOAuth2>;

  using Command = aurora::mail::common::CommandChecker<
      Helo,
      Ehlo,
      StartTls,
      MailFrom,
      RcptTo,
      Data,
      Quit,
      Rset,
      Noop,
      AuthPlain,
      AuthLogin,
      AuthXOAuth2,
      Vrfy,
      Help>::type;

  inline Result<std::string> serialize(const Command& cmd)
  {
    return std::visit([](const auto& c) -> Result<std::string> { return c.serialize(); }, cmd);
  }

  inline Result<std::string> serialize(const AuthVariant& auth)
  {
    return std::visit([](const auto& a) -> Result<std::string> { return a.serialize(); }, auth);
  }

  template<aurora::mail::common::Serializable T>
  inline Result<std::string> serialize(const T& cmd)
  {
    return cmd.serialize();
  }

}  // namespace aurora::mail::smtp::command

#endif  // SMTP_COMMAND_HPP
