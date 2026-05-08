#include "SmtpClient.hpp"

#include <Base64.hpp>
#include <LoggerInstance.hpp>
#include <MimeWriter.hpp>
#include <format>

#include "Parser.hpp"
#include "ResponseType.hpp"
#include "SmtpCommand.hpp"

namespace aurora::mail::smtp
{

  using response::SmtpResponse;

  ConnectionMode SmtpClient::portToConnectionMode(uint16_t port)
  {
    switch (port)
    {
      case 465: return ConnectionMode::SSL_TLS;
      case 587: return ConnectionMode::STARTTLS;
      case 25: return ConnectionMode::PLAIN;
      default: return ConnectionMode::STARTTLS;
    }
  }

  /**
   * @brief Parse EHLO response into SmtpCapabilities structure.
   *
   * The SmtpResponse.text already has the "250-" prefixes stripped by the parser.
   * Format after parsing:
   *   smtp.gmail.com at your service, [client]
   *   SIZE 52428800
   *   8BITMIME
   *   STARTTLS
   *   AUTH PLAIN LOGIN XOAUTH2
   *   PIPELINING
   *
   * @param response The parsed SMTP response from EHLO command
   * @param caps Output capabilities structure to populate
   */
  void parseEhloCapabilities(const SmtpResponse& response, SmtpCapabilities& caps);

  awaitable<VoidResult> SmtpClient::asyncConnect(const std::string& server, uint16_t port)
  {
    // Determine connection mode based on standard SMTP ports
    ConnectionMode mode = portToConnectionMode(port);
    bool use_implicit_tls = (mode == ConnectionMode::SSL_TLS);

    // Establish connection (with TLS immediately for port 465)
    auto result = co_await establishConnection(server, port, use_implicit_tls);
    if (!result)
    {
      co_return std::unexpected(result.error());
    }

    // Read and check greeting
    auto greeting_result = co_await readSmtpResponse();
    if (!greeting_result.has_value())
    {
      co_return std::unexpected(greeting_result.error());
    }

    auto greeting_parsed = parseResponse(greeting_result.value());
    if (!greeting_parsed.has_value())
    {
      co_return std::unexpected(greeting_parsed.error());
    }

    if (!greeting_parsed->isSuccess())
    {
      co_return std::unexpected(
          ProtocolError::protocol(
              std::format("SMTP connection rejected (code {})", greeting_parsed->code), greeting_parsed->text));
    }

    log_debug(std::format("SMTP: Greeting OK (code: {})", greeting_parsed->code));

    // Send EHLO and parse capabilities
    auto ehlo_result = co_await sendCommandWithResponse(command::Ehlo{ "localhost" });
    if (!ehlo_result.has_value())
    {
      co_return std::unexpected(ehlo_result.error());
    }

    log_debug("SMTP: EHLO command successful.");
    parseEhloCapabilities(ehlo_result.value(), capabilities_);

    // STARTTLS upgrade if needed and supported
    if (mode == ConnectionMode::STARTTLS && capabilities_.hasStartTls())
    {
      auto starttls_result = co_await sendCommand(command::StartTls{});
      if (!starttls_result.has_value())
      {
        co_return std::unexpected(starttls_result.error());
      }

      auto upgrade_result = co_await upgradeToTLS();
      if (!upgrade_result.has_value())
      {
        co_return std::unexpected(upgrade_result.error());
      }

      // RFC 3207: After STARTTLS, send EHLO again to refresh capabilities
      auto ehlo2_result = co_await sendCommandWithResponse(command::Ehlo{ "localhost" });
      if (!ehlo2_result.has_value())
      {
        co_return std::unexpected(ehlo2_result.error());
      }

      // Re-parse capabilities after TLS upgrade (may have more options now)
      parseEhloCapabilities(ehlo2_result.value(), capabilities_);
    }

    log_info(std::format("SMTP: Successfully connected with capabilities: {}", capabilities_.toString()));
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncSendMail(const common::mail::MailMessage& mail_message)
  {
    // MAIL FROM
    auto mail_from_result = co_await sendCommand(command::MailFrom{ mail_message.from.getAddress() });
    if (!mail_from_result.has_value())
    {
      co_return std::unexpected(mail_from_result.error());
    }

    // RCPT TO for all recipients (To, CC, BCC)
    // SMTP envelope includes all recipients regardless of header type
    for (const auto& recipient : mail_message.email_recipients.all())
    {
      auto rcpt_result = co_await sendCommand(command::RcptTo{ recipient.getAddress() });
      if (!rcpt_result.has_value())
      {
        co_return std::unexpected(rcpt_result.error());
      }
    }

    // DATA - expects 3xx intermediate response
    auto data_result = co_await sendCommand(command::Data{}, true);
    if (!data_result.has_value())
    {
      co_return std::unexpected(data_result.error());
    }

    // Send message body
    std::string message = common::mime::writer::buildMimeMessage(mail_message);

    auto write_result = co_await writeCommand(message);
    if (!write_result.has_value())
    {
      co_return std::unexpected(write_result.error());
    }

    auto end_result = co_await writeCommand("\r\n.\r\n");
    if (!end_result.has_value())
    {
      co_return std::unexpected(end_result.error());
    }

    // Read and validate completion response
    auto completion_result = co_await readSmtpResponse();
    if (!completion_result.has_value())
    {
      co_return std::unexpected(completion_result.error());
    }

    auto completion_parsed = parseResponse(completion_result.value());
    if (!completion_parsed.has_value())
    {
      co_return std::unexpected(completion_parsed.error());
    }

    if (!completion_parsed->isSuccess())
    {
      co_return std::unexpected(
          ProtocolError::protocol(
              std::format("SMTP mail data rejected (code {})", completion_parsed->code), completion_parsed->text));
    }

    log_info(std::format("SMTP: Mail sent successfully (code: {})", completion_parsed->code));
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncNoop()
  {
    auto result = co_await sendCommand(command::Noop{});
    if (!result.has_value())
    {
      co_return std::unexpected(result.error());
    }

    log_info("SMTP: NOOP command successful.");
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncHelp()
  {
    auto result = co_await sendCommand(command::Help{});
    if (!result.has_value())
    {
      co_return std::unexpected(result.error());
    }

    log_info("SMTP: HELP command successful.");
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncVrfy(const std::string& address)
  {
    auto result = co_await sendCommand(command::Vrfy{ address });
    if (!result.has_value())
    {
      co_return std::unexpected(result.error());
    }

    log_info(std::format("SMTP: VRFY command successful for address: {}", address));
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncRset()
  {
    auto result = co_await sendCommand(command::Rset{});
    if (!result.has_value())
    {
      co_return std::unexpected(result.error());
    }

    log_info("SMTP: RSET command successful.");
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncQuit()
  {
    auto result = co_await sendCommand(command::Quit{});

    if (result.has_value())
    {
      co_await closeConnection();
    }

    log_info("SMTP: QUIT command successful.");
    co_return VoidResult{};
  }

  awaitable<VoidResult> SmtpClient::asyncAuthenticate(const command::AuthLogin& auth)
  {
    // Validate inputs once up-front; this also catches CRLF injection attempts
    // before any wire bytes are sent. Note: serialize() returns only the
    // initial "AUTH LOGIN\r\n"; the credential lines are emitted manually
    // below as per RFC 4954.
    auto initial = auth.serialize();
    if (!initial.has_value())
    {
      co_return std::unexpected(initial.error());
    }

    // Step 1: send "AUTH LOGIN" and expect a 334 challenge.
    auto step1 = co_await BaseProtocolClient::sendCommandAndReadResponse(initial.value(), isSmtpFinalLine, 20);
    if (!step1.has_value())
    {
      co_return std::unexpected(step1.error());
    }
    auto step1_parsed = parseResponse(step1.value());
    if (!step1_parsed.has_value())
    {
      co_return std::unexpected(step1_parsed.error());
    }
    if (!step1_parsed->needsMoreInput())  // expect 3xx (334)
    {
      co_return std::unexpected(
          ProtocolError::auth(
              std::format("AUTH LOGIN: server did not request username (code {})", step1_parsed->code), step1_parsed->text));
    }

    // Step 2: respond with base64(username) and expect another 334 challenge.
    std::string user_b64 = aurora::mail::common::base64::base64Encode(auth.username);
    user_b64 += "\r\n";
    auto step2 = co_await BaseProtocolClient::sendCommandAndReadResponse(user_b64, isSmtpFinalLine, 20);
    if (!step2.has_value())
    {
      co_return std::unexpected(step2.error());
    }
    auto step2_parsed = parseResponse(step2.value());
    if (!step2_parsed.has_value())
    {
      co_return std::unexpected(step2_parsed.error());
    }
    if (!step2_parsed->needsMoreInput())
    {
      co_return std::unexpected(
          ProtocolError::auth(
              std::format("AUTH LOGIN: server did not accept username (code {})", step2_parsed->code), step2_parsed->text));
    }

    // Step 3: respond with base64(password) and expect 235 success.
    std::string pass_b64 = aurora::mail::common::base64::base64Encode(auth.password);
    pass_b64 += "\r\n";
    auto step3 = co_await BaseProtocolClient::sendCommandAndReadResponse(pass_b64, isSmtpFinalLine, 20);
    if (!step3.has_value())
    {
      co_return std::unexpected(step3.error());
    }
    auto step3_parsed = parseResponse(step3.value());
    if (!step3_parsed.has_value())
    {
      co_return std::unexpected(step3_parsed.error());
    }
    if (!step3_parsed->isSuccess())
    {
      co_return std::unexpected(
          ProtocolError::auth(
              std::format("AUTH LOGIN: authentication rejected (code {})", step3_parsed->code), step3_parsed->text));
    }

    log_info("SMTP: AUTH LOGIN succeeded");
    co_return VoidResult{};
  }

  awaitable<Result<std::string>> SmtpClient::readSmtpResponse()
  {
    co_return co_await readResponse(isSmtpFinalLine, 20);
  }

  Result<SmtpResponse> SmtpClient::parseResponse(const std::string& raw_response)
  {
    // Parse response using monadic error handling
    auto parse_result = response::parse(raw_response)
                            .or_else(
                                [](const std::string& error) -> std::expected<SmtpResponse, std::string>
                                { return std::unexpected(std::format("Failed to parse SMTP response: {}", error)); });

    if (!parse_result.has_value())
    {
      return std::unexpected(ProtocolError::protocol("SMTP response parsing failed", parse_result.error()));
    }

    return parse_result.value();
  }

  void parseEhloCapabilities(const SmtpResponse& response, SmtpCapabilities& caps)
  {
    caps.clear();
    std::stringstream ss(response.text);
    std::string line;

    // Skip the first line (server greeting like "smtp.gmail.com at your service")
    std::getline(ss, line);

    while (std::getline(ss, line))
    {
      // Clean up carriage return if present
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }

      // Skip empty lines
      if (line.empty())
      {
        continue;
      }

      // Line is already stripped of "250-" prefix by the SMTP parser
      // Format: "KEYWORD" or "KEYWORD params"
      auto space_pos = line.find(' ');
      std::string keyword = line.substr(0, space_pos);

      // Convert keyword to uppercase for case-insensitive matching
      std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper);

      caps.extensions.insert(keyword);

      // Parse parameters if present
      if (space_pos != std::string::npos)
      {
        std::string value = line.substr(space_pos + 1);
        caps.parameters[keyword] = value;

        // Special handling for AUTH: parse mechanisms into list
        if (keyword == "AUTH")
        {
          std::istringstream auth_ss(value);
          std::string mechanism;
          while (auth_ss >> mechanism)
          {
            std::transform(mechanism.begin(), mechanism.end(), mechanism.begin(), ::toupper);
            caps.authMechanisms.push_back(mechanism);
          }
        }
      }
    }

    log_debug(
        std::format("SMTP: Parsed {} extensions, {} auth mechanisms", caps.extensions.size(), caps.authMechanisms.size()));
  }
}  // namespace aurora::mail::smtp
