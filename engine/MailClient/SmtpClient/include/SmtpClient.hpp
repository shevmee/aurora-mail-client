#ifndef SMTP_CLIENT_HPP
#define SMTP_CLIENT_HPP

#include <BaseProtocolClient.hpp>
#include <LoggerInstance.hpp>
#include <ProtocolConcepts.hpp>
#include <ProtocolError.hpp>
#include <StartupConfig.hpp>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <format>
#include <unordered_set>
#include <vector>

#include "MailMessage.hpp"
#include "ResponseType.hpp"
#include "SmtpCommand.hpp"

namespace aurora::mail::smtp
{

  using boost::asio::awaitable;
  using common::BaseProtocolClient;
  using common::ConnectionMode;
  using common::MailProtocol;
  using common::ProtocolCommand;
  using common::ProtocolError;
  using common::Result;
  using common::VoidResult;

  /**
   * @brief SMTP server capabilities parsed from EHLO response.
   *
   * Common SMTP extensions (RFC 5321, RFC 3207, RFC 4954, etc.):
   * - STARTTLS: TLS encryption upgrade (RFC 3207)
   * - AUTH: Authentication mechanisms (RFC 4954)
   * - SIZE: Maximum message size (RFC 1870)
   * - 8BITMIME: 8-bit MIME transport (RFC 6152)
   * - PIPELINING: Command pipelining (RFC 2920)
   * - CHUNKING: Chunked transfer (RFC 3030)
   * - SMTPUTF8: UTF-8 in envelope (RFC 6531)
   * - DSN: Delivery Status Notifications (RFC 3461)
   * - ENHANCEDSTATUSCODES: Enhanced status codes (RFC 2034)
   */
  struct SmtpCapabilities
  {
    std::unordered_set<std::string> extensions;
    std::unordered_map<std::string, std::string> parameters;
    std::vector<std::string> authMechanisms;

    /**
     * @brief Check if a specific extension is supported.
     * @param extension Extension name (case-insensitive, stored uppercase)
     */
    bool supports(const std::string& extension) const
    {
      return extensions.count(extension) > 0;
    }

    /**
     * @brief Get parameter value for an extension.
     * @param extension Extension name
     * @return Parameter value or empty string if not found
     */
    std::string getParameter(const std::string& extension) const
    {
      auto it = parameters.find(extension);
      return it != parameters.end() ? it->second : "";
    }

    // --- Common capability checks ---

    bool hasStartTls() const
    {
      return supports("STARTTLS");
    }
    bool has8BitMime() const
    {
      return supports("8BITMIME");
    }
    bool hasPipelining() const
    {
      return supports("PIPELINING");
    }
    bool hasChunking() const
    {
      return supports("CHUNKING");
    }
    bool hasSmtpUtf8() const
    {
      return supports("SMTPUTF8");
    }
    bool hasDsn() const
    {
      return supports("DSN");
    }
    bool hasEnhancedStatusCodes() const
    {
      return supports("ENHANCEDSTATUSCODES");
    }
    bool hasBinaryMime() const
    {
      return supports("BINARYMIME");
    }

    /**
     * @brief Check if server supports any authentication.
     */
    bool hasAuth() const
    {
      return supports("AUTH") && !authMechanisms.empty();
    }

    /**
     * @brief Check if a specific auth mechanism is supported.
     * @param mechanism Mechanism name (PLAIN, LOGIN, XOAUTH2, etc.)
     */
    bool supportsAuthMechanism(const std::string& mechanism) const
    {
      std::string upper = mechanism;
      std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
      return std::find(authMechanisms.begin(), authMechanisms.end(), upper) != authMechanisms.end();
    }

    bool hasPlainAuth() const
    {
      return supportsAuthMechanism("PLAIN");
    }
    bool hasLoginAuth() const
    {
      return supportsAuthMechanism("LOGIN");
    }
    bool hasXOAuth2() const
    {
      return supportsAuthMechanism("XOAUTH2");
    }
    bool hasCramMd5() const
    {
      return supportsAuthMechanism("CRAM-MD5");
    }

    /**
     * @brief Get list of supported authentication mechanisms.
     */
    const std::vector<std::string>& getSupportedAuthMechanisms() const
    {
      return authMechanisms;
    }

    /**
     * @brief Get maximum message size in bytes (from SIZE extension).
     * @return Max size, or 0 if not specified (no limit or unknown)
     */
    size_t getMaxMessageSize() const
    {
      auto it = parameters.find("SIZE");
      if (it != parameters.end() && !it->second.empty())
      {
        try
        {
          return std::stoull(it->second);
        }
        catch (...)
        {
          return 0;
        }
      }
      return 0;
    }

    /**
     * @brief Clear all stored capabilities.
     */
    void clear()
    {
      extensions.clear();
      parameters.clear();
      authMechanisms.clear();
    }

    /**
     * @brief Get a human-readable summary of capabilities.
     */
    std::string toString() const
    {
      std::string result = "SMTP Capabilities:\n";
      for (const auto& ext : extensions)
      {
        result += "  " + ext;
        auto it = parameters.find(ext);
        if (it != parameters.end() && !it->second.empty())
        {
          result += " " + it->second;
        }
        result += "\n";
      }
      if (!authMechanisms.empty())
      {
        result += "  Auth mechanisms: ";
        for (size_t i = 0; i < authMechanisms.size(); ++i)
        {
          if (i > 0)
            result += ", ";
          result += authMechanisms[i];
        }
        result += "\n";
      }
      return result;
    }
  };

  /**
   * @brief SMTP client implementation using BaseProtocolClient.
   *
   * Features:
   * - Shares common infrastructure with ImapClient via BaseProtocolClient
   * - No duplicated read/write/TLS logic
   * - Consistent error handling with Result<T>
   * - Protocol-specific logic clearly separated
   */
  class SmtpClient : public BaseProtocolClient
  {
   public:
    SmtpClient(boost::asio::io_context& io_context, boost::asio::ssl::context& ssl_context, int timeout_seconds)
        : BaseProtocolClient(io_context, ssl_context, timeout_seconds, MailProtocol::SMTP)
    {
    }

    /**
     * @brief Connects to the SMTP server asynchronously.
     *
     * Connection mode is automatically determined from port:
     * - Port 465: SMTPS (direct TLS)
     * - Port 587: STARTTLS (upgrade to TLS)
     * - Port 25: STARTTLS (default, secure)
     * - Other: STARTTLS (safe default)
     *
     * @param hostname Server hostname or IP
     * @param port Server port
     * @return VoidResult indicating success or error
     */
    awaitable<VoidResult> asyncConnect(const std::string& hostname, uint16_t port);

    /**
     * @brief Authenticate with the server using any supported auth method.
     *
     * Accepts any authentication command type (AuthPlain, AuthLogin,
     * AuthXOAuth2). The authentication method is automatically dispatched based
     * on the command type.
     *
     * @tparam AuthCmd Authentication command type (must satisfy ProtocolCommand)
     * @param auth Authentication command (AuthPlain, AuthLogin, or AuthXOAuth2)
     * @return VoidResult indicating success or error
     *
     * @example
     *   // PLAIN authentication
     *   co_await client.asyncAuthenticate(
     *       smtp::command::AuthPlain{"user@example.com", "password"});
     *
     *   // LOGIN authentication
     *   co_await client.asyncAuthenticate(
     *       smtp::command::AuthLogin{"user@example.com", "password"});
     */
    template<ProtocolCommand AuthCmd>
    awaitable<VoidResult> asyncAuthenticate(const AuthCmd& auth)
    {
      co_return co_await sendCommand(auth);
    }

    /**
     * @brief Authenticate with AUTH LOGIN (3-step challenge/response).
     *
     * Drives the full RFC 4954 LOGIN handshake:
     *   1. C: AUTH LOGIN
     *      S: 334 VXNlcm5hbWU6     (base64("Username:"))
     *   2. C: base64(username)
     *      S: 334 UGFzc3dvcmQ6     (base64("Password:"))
     *   3. C: base64(password)
     *      S: 235 Authentication successful
     *
     * The previous template overload only ever transmitted step 1, leaving
     * the user/password fields silently unused. This non-template overload
     * is selected for AuthLogin via overload resolution.
     */
    awaitable<VoidResult> asyncAuthenticate(const command::AuthLogin& auth);

    /**
     * @brief Send a complete mail message.
     *
     * @param mail_message The message to send
     * @return VoidResult indicating success or error
     */
    awaitable<VoidResult> asyncSendMail(const common::mail::MailMessage& mail_message);

    awaitable<VoidResult> asyncNoop();
    awaitable<VoidResult> asyncHelp();
    awaitable<VoidResult> asyncVrfy(const std::string& address);
    awaitable<VoidResult> asyncRset();

    /**
     * @brief Send QUIT command and close connection.
     *
     * @return VoidResult indicating success or error
     */
    awaitable<VoidResult> asyncQuit();

    /**
     * @brief Get server capabilities parsed from EHLO response.
     *
     * Available after successful asyncConnect().
     */
    const SmtpCapabilities& getCapabilities() const
    {
      return capabilities_;
    }

   private:
    /**
     * @brief SMTP-specific response reader.
     *
     * Uses BaseProtocolClient::readResponse() with SMTP predicate.
     */
    awaitable<Result<std::string>> readSmtpResponse();

    /**
     * @brief SMTP-specific: Check if line is final (space at position 3).
     */
    static bool isSmtpFinalLine(const std::string& line)
    {
      // SMTP format: "250 OK" (final) vs "250-Extended" (continuation)
      return line.length() >= 4 && line[3] == ' ';
    }

    /**
     * @brief Determine SMTP connection mode from port number.
     *
     * Standard ports:
     * - 465: SMTPS (direct TLS)
     * - 587: STARTTLS (upgrade to TLS)
     * - 25: STARTTLS (default, secure)
     * - Other: STARTTLS (safe default)
     */
    ConnectionMode portToConnectionMode(uint16_t port);

    /**
     * @brief Parse SMTP response from raw text.
     */
    Result<response::SmtpResponse> parseResponse(const std::string& raw_response);

    /**
     * @brief Send command, read response, parse and check for success.
     *
     * Template method that accepts any SMTP command struct with serialize()
     * and name() methods. Automatically validates 2xx success responses.
     *
     * @param command SMTP command (Ehlo, MailFrom, Data, etc.)
     * @param expect_intermediate If true, accepts 3xx responses (for DATA
     * command)
     * @return VoidResult indicating success or error with details
     */
    template<ProtocolCommand Cmd>
    awaitable<VoidResult> sendCommand(const Cmd& command, bool expect_intermediate = false)
    {
      auto result = co_await sendCommandWithResponse(command, expect_intermediate);
      if (!result.has_value())
      {
        co_return std::unexpected(result.error());
      }
      co_return VoidResult{};
    }

    /**
     * @brief Send command and return the parsed response.
     *
     * Use this when you need access to the response data (e.g., EHLO for
     * capabilities).
     *
     * @param command SMTP command
     * @param expect_intermediate If true, accepts 3xx responses
     * @return Parsed SmtpResponse or error
     */
    template<ProtocolCommand Cmd>
    awaitable<Result<response::SmtpResponse>> sendCommandWithResponse(const Cmd& command, bool expect_intermediate = false)
    {
      auto serialized_result = command.serialize();
      if (!serialized_result.has_value())
      {
        co_return std::unexpected(serialized_result.error());
      }
      std::string_view cmd_name = Cmd::name();

      // Send command and read response
      auto response =
          co_await BaseProtocolClient::sendCommandAndReadResponse(serialized_result.value(), isSmtpFinalLine, 20);

      if (!response.has_value())
      {
        co_return std::unexpected(response.error());
      }

      // Parse response
      auto parsed = parseResponse(response.value());
      if (!parsed.has_value())
      {
        co_return std::unexpected(parsed.error());
      }

      // Check response code
      const auto& resp = parsed.value();
      bool valid = expect_intermediate ? resp.needsMoreInput() : resp.isSuccess();

      if (!valid)
      {
        co_return std::unexpected(
            ProtocolError::protocol(std::format("SMTP {} command failed (code {})", cmd_name, resp.code), resp.text));
      }

      log_debug(std::format("SMTP: {} succeeded (code: {})", cmd_name, resp.code));
      co_return resp;
    }

   private:
    SmtpCapabilities capabilities_;
  };

}  // namespace aurora::mail::smtp

#endif  // SMTP_CLIENT_HPP
