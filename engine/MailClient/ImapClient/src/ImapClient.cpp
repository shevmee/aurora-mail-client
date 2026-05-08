#include "ImapClient.hpp"

#include <Base64.hpp>
#include <ImapTokenizer.hpp>
#include <ImapUtf7.hpp>
#include <LoggerInstance.hpp>
#include <MailMessage.hpp>
#include <StartupConfig.hpp>
#include <Stream.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <string_view>
#include <variant>

#include "ImapCommand.hpp"
#include "ImapParser.hpp"
#include "ImapResponse.hpp"

namespace aurora::mail::imap
{
  namespace
  {
    using aurora::mail::common::stream::TimedOutStream;

    /** TimedOutStream wraps each readUntil with timeout_; IMAP IDLE may have no server traffic for minutes. */
    struct IdleLongPollReadGuard
    {
      TimedOutStream* ts = nullptr;
      std::chrono::milliseconds prev{};

      explicit IdleLongPollReadGuard(TimedOutStream* s) : ts(s)
      {
        if (ts != nullptr)
        {
          prev = ts->timeout();
          ts->set_timeout(std::chrono::hours(24));
        }
      }
      IdleLongPollReadGuard(const IdleLongPollReadGuard&) = delete;
      IdleLongPollReadGuard& operator=(const IdleLongPollReadGuard&) = delete;
      ~IdleLongPollReadGuard()
      {
        if (ts != nullptr)
        {
          ts->set_timeout(prev);
        }
      }
    };

    std::string_view trim_crlf(std::string_view s)
    {
      while (s.size() >= 2 && s.ends_with("\r\n"))
      {
        s.remove_suffix(2);
      }
      while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
      {
        s.remove_suffix(1);
      }
      return s;
    }

    /** Payload after '+' on an IMAP AUTHENTICATE continuation line (often base64 JSON for XOAUTH2 errors). */
    std::string_view plus_continuation_payload(std::string_view line)
    {
      line = trim_crlf(line);
      if (line.empty() || line.front() != '+')
      {
        return {};
      }
      line.remove_prefix(1);
      if (!line.empty() && line.front() == ' ')
      {
        line.remove_prefix(1);
      }
      return line;
    }
  }  // namespace

  namespace asio = boost::asio;
  using boost::system::error_code;

  ConnectionMode ImapClient::portToConnectionMode(uint16_t port)
  {
    switch (port)
    {
      case 993: return ConnectionMode::SSL_TLS;
      case 143:
      default: return ConnectionMode::STARTTLS;
    }
  }

  std::optional<std::size_t> ImapClient::detectLiteralSize(std::string_view line) const
  {
    if (line.empty())
    {
      return std::nullopt;
    }

    std::size_t pos = line.size();
    while (pos > 0 && std::isspace(static_cast<unsigned char>(line[pos - 1])) != 0)
    {
      --pos;
    }

    if (pos == 0 || line[pos - 1] != '}')
    {
      return std::nullopt;
    }
    --pos;

    std::size_t digit_end = pos;
    while (pos > 0 && std::isdigit(static_cast<unsigned char>(line[pos - 1])) != 0)
    {
      --pos;
    }

    if (pos == digit_end)
    {
      return std::nullopt;
    }

    std::size_t result = 0;
    auto [ptr, ec] = std::from_chars(line.data() + pos, line.data() + digit_end, result);
    if (ec != std::errc{})
    {
      return std::nullopt;
    }

    return result;
  }

  awaitable<Result<std::string>> ImapClient::readGreeting()
  {
    // Use the parser's isGreetingLine function to determine when greeting is
    // complete
    auto is_greeting_complete = [](const std::string& line) -> bool { return response::isGreetingLine(line); };

    co_return co_await readResponse(is_greeting_complete, 10);
  }

  bool ImapClient::isResponseFinalForSerializedCommand(std::string_view serialized_command, const std::string& line)
  {
    if (!isImapFinalLine(line))
    {
      return false;
    }
    const auto sp = serialized_command.find(' ');
    if (sp == std::string_view::npos || sp == 0)
    {
      return true;
    }
    const std::string_view cmd_tag = serialized_command.substr(0, sp);
    if (line.size() < cmd_tag.size())
    {
      return false;
    }
    if (line.compare(0, cmd_tag.size(), cmd_tag) != 0)
    {
      return false;
    }
    return line.size() == cmd_tag.size() || std::isspace(static_cast<unsigned char>(line[cmd_tag.size()])) != 0;
  }

  awaitable<Result<std::string>> ImapClient::sendCommandAndReadResponse(const command::Command& command)
  {
    auto serialized_result = command::serialize(command);
    if (!serialized_result.has_value())
    {
      co_return std::unexpected(serialized_result.error());
    }
    std::string serialized = std::move(serialized_result).value();
    co_return co_await BaseProtocolClient::sendCommandAndReadResponse(
        serialized, [serialized](const std::string& line) { return isResponseFinalForSerializedCommand(serialized, line); });
  }

  Result<response::ImapResponse> ImapClient::parseAndValidate(const std::string& raw_response, StatusType expected_status)
  {
    auto parse_result = response::parse(raw_response);
    if (!parse_result.has_value())
    {
      return std::unexpected(ProtocolError::protocol("Failed to parse IMAP response", parse_result.error()));
    }

    auto& resp = parse_result.value();

    // Process untagged responses (may trigger callbacks)
    processUntaggedResponses(resp);

    // Check status directly (consistent with SMTP approach)
    if (resp.status != expected_status)
    {
      return std::unexpected(
          ProtocolError::protocol(
              std::format(
                  "IMAP status mismatch: expected {}, got {}",
                  response::statusTypeToString(expected_status),
                  response::statusTypeToString(resp.status)),
              std::string(resp.text)));
    }

    return std::move(resp);
  }

  void ImapClient::processUntaggedResponses(const response::ImapResponse& response)
  {
    if (!unsolicited_callback_)
    {
      return;  // No callback registered
    }

    // Categorize untagged responses:
    // - Expected data (FETCH in response to FETCH command) - don't callback
    // - Unsolicited notifications (EXISTS, EXPUNGE, etc.) - callback
    //
    // For now, we'll callback for specific notification types:
    // EXISTS, RECENT, EXPUNGE, FLAGS, ALERT, BYE

    for (const auto& untagged : response.untagged)
    {
      // Check if this is a server notification (not command data)
      if (untagged.command == "EXISTS" || untagged.command == "RECENT" || untagged.command == "EXPUNGE" ||
          untagged.command == "FLAGS" || untagged.command == "BYE" || untagged.data.find("[ALERT]") != std::string::npos)
      {
        log_info(std::format("IMAP: Unsolicited server notification: {} {}", untagged.command, untagged.data));

        (*unsolicited_callback_)(untagged);
      }
    }
  }

  awaitable<VoidResult> ImapClient::asyncConnect(const std::string& server, uint16_t port)
  {
    ConnectionMode mode = portToConnectionMode(port);
    bool use_implicit_tls = (mode == ConnectionMode::SSL_TLS);

    // Establish connection (with TLS immediately for port 993)
    auto conn_result = co_await establishConnection(server, port, use_implicit_tls);
    if (!conn_result.has_value())
    {
      co_return std::unexpected(conn_result.error());
    }

    // Read and validate server greeting using dedicated parser function
    auto greeting_raw = co_await readGreeting();
    if (!greeting_raw.has_value())
    {
      co_return std::unexpected(greeting_raw.error());
    }

    // Log greeting (strip trailing CRLF to avoid blank line in logs)
    std::string greeting_log = greeting_raw.value();
    if (greeting_log.ends_with("\r\n"))
    {
      greeting_log.resize(greeting_log.size() - 2);
    }
    log_info(std::format("IMAP: Server greeting: {}", greeting_log));

    // Use the parser with is_greeting=true parameter
    auto greeting_result = response::parse(greeting_raw.value(), true);
    if (!greeting_result.has_value())
    {
      co_return std::unexpected(ProtocolError::protocol("Failed to parse IMAP greeting", greeting_result.error()));
    }

    // Validate the greeting status (parser already validated it's OK/PREAUTH/BYE)
    auto& parsed_greeting = greeting_result.value();
    if (parsed_greeting.status != StatusType::OK && parsed_greeting.status != StatusType::PREAUTH)
    {
      co_return std::unexpected(ProtocolError::protocol("Server refused connection", std::string(parsed_greeting.text)));
    }

    // STARTTLS upgrade if needed (port 143)
    if (mode == ConnectionMode::STARTTLS)
    {
      // Request capabilities to check if STARTTLS is supported
      auto caps_result = co_await asyncCapability();
      if (!caps_result.has_value())
      {
        co_return std::unexpected(caps_result.error());
      }

      if (!caps_result->hasStartTls())
      {
        co_return std::unexpected(ProtocolError::protocol("Server does not support STARTTLS"));
      }

      std::string tag = m_tag_generator.next();
      auto starttls_result = co_await sendCommandAndValidate(command::StartTls{ tag }, StatusType::OK);

      if (!starttls_result.has_value())
      {
        co_return std::unexpected(starttls_result.error());
      }

      auto upgrade_result = co_await upgradeToTLS();
      if (!upgrade_result.has_value())
      {
        co_return std::unexpected(upgrade_result.error());
      }

      // RFC 2595: Re-request capabilities after STARTTLS (may have more options)
      auto caps_after_tls = co_await asyncCapability();
      if (caps_after_tls.has_value())
      {
        capabilities_ = caps_after_tls.value();
      }
      else
      {
        // Use pre-TLS capabilities if refresh fails
        capabilities_ = caps_result.value();
      }
    }

    log_info("IMAP: Successfully connected");
    co_return VoidResult{};
  }

  awaitable<VoidResult> ImapClient::asyncLogin(const std::string& username, const std::string& password)
  {
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Login{ tag, username, password }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncAuthenticate(const command::AuthXOAuth2& auth)
  {
    // SASL-IR form (RFC 4959): the credential is sent inline with the command,
    // so there is no initial "+" continuation to wait for. All input
    // validation and base64 SASL framing live inside auth.serialize().
    auto serialized = auth.serialize();
    if (!serialized.has_value())
    {
      co_return std::unexpected(serialized.error());
    }

    auto write_result = co_await writeCommand(serialized.value());
    if (!write_result.has_value())
    {
      co_return write_result;
    }

    // Gmail (and some other XOAUTH2 implementations) reply to a bad credential
    // with a "+" continuation containing a base64-encoded JSON error blob, then
    // wait for an empty client line before issuing the tagged NO. Reading only
    // for the tagged line would deadlock both sides, so we explicitly handle
    // the continuation here.
    while (true)
    {
      auto lineRes = co_await readResponse([](const std::string&) { return true; });
      if (!lineRes.has_value())
      {
        co_return std::unexpected(lineRes.error());
      }
      const std::string& line = lineRes.value();

      if (!line.empty() && line[0] == '+')
      {
        std::string_view payload = plus_continuation_payload(line);
        if (!payload.empty())
        {
          std::string decoded = aurora::mail::common::base64::base64Decode(payload);
          log_warn(std::format("IMAP: XOAUTH2 server continuation (decoded): {}", decoded));
        }
        else
        {
          log_warn("IMAP: empty XOAUTH2 continuation (+) after credential; sending empty client line");
        }
        auto ack = co_await writeCommand("\r\n");
        if (!ack.has_value())
        {
          co_return ack;
        }
        continue;
      }

      if (!line.empty() && line[0] == '*')
      {
        if (auto parseOne = response::parse(line); parseOne.has_value())
        {
          processUntaggedResponses(parseOne.value());
        }
        continue;
      }

      if (isImapFinalLine(line))
      {
        auto parsed = parseAndValidate(line, StatusType::OK);
        if (!parsed.has_value())
        {
          co_return std::unexpected(parsed.error());
        }
        log_debug("IMAP: AUTHENTICATE XOAUTH2 succeeded");
        co_return VoidResult{};
      }

      co_return std::unexpected(
          ProtocolError::protocol(std::format("Unexpected IMAP response after XOAUTH2 credential: {}", line)));
    }
  }

  awaitable<VoidResult> ImapClient::asyncLogout()
  {
    std::string tag = m_tag_generator.next();
    auto result = co_await sendCommandAndValidate(command::Logout{ tag }, StatusType::OK);

    if (result.has_value())
    {
      co_await closeConnection();
    }

    co_return result;
  }

  awaitable<VoidResult> ImapClient::asyncSelectMailbox(const std::string& mailbox)
  {
    std::string tag = m_tag_generator.next();
    auto result = co_await sendCommandAndValidate(command::Select{ tag, mailbox }, StatusType::OK);

    if (result.has_value())
    {
      m_current_mailbox = mailbox;
      log_info(std::format("IMAP: Mailbox '{}' selected", mailbox));
    }

    co_return result;
  }

  awaitable<VoidResult> ImapClient::asyncExamineMailbox(const std::string& mailbox)
  {
    std::string tag = m_tag_generator.next();
    auto result = co_await sendCommandAndValidate(command::Examine{ tag, mailbox }, StatusType::OK);

    if (result.has_value())
    {
      m_current_mailbox = mailbox;
      log_info(std::format("IMAP: Mailbox '{}' examined", mailbox));
    }

    co_return result;
  }

  awaitable<Result<std::string>> ImapClient::asyncFetchMail(const std::string& message_set, const std::string& data_items)
  {
    log_info(std::format("IMAP: FETCH {} {}", message_set, data_items));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::Fetch{ tag, message_set, data_items });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Validate response status
    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: FETCH completed");
    co_return response.value();
  }

  awaitable<Result<std::string>> ImapClient::asyncSearchMail(const std::string& criteria)
  {
    log_info(std::format("IMAP: SEARCH {}", criteria));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::Search{ tag, criteria });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: SEARCH completed");
    co_return response.value();
  }

  // Helper: Parse a single LIST response line using IMAP tokenizer
  // Format: * LIST (\HasNoChildren \Trash) "/" "Trash"
  static std::optional<MailboxInfo> parseListLine(const std::string& line)
  {
    if (!line.starts_with("* LIST "))
    {
      return std::nullopt;
    }

    MailboxInfo info;

    // Skip "* LIST " and tokenize the rest
    const std::string_view data(line.data() + 7, line.size() - 7);
    parser::Tokenizer tokenizer(data);

    // Parse attributes list: (\HasNoChildren \Trash)
    auto attrs_result = tokenizer.nextValue();
    if (!attrs_result.has_value())
    {
      return std::nullopt;
    }

    if (auto* list = std::get_if<std::unique_ptr<parser::List>>(&attrs_result.value()))
    {
      for (const auto& item : (*list)->items)
      {
        if (auto* atom = std::get_if<parser::Atom>(&item))
        {
          info.attributes.emplace_back(atom->value);
        }
      }
    }

    // Parse delimiter: "/" or NIL
    auto delim_result = tokenizer.nextValue();
    if (delim_result.has_value())
    {
      if (auto* quoted = std::get_if<parser::Quoted>(&delim_result.value()))
      {
        info.delimiter = quoted->value;
      }
      else if (auto* atom = std::get_if<parser::Atom>(&delim_result.value()))
      {
        if (atom->value == "NIL")
        {
          info.delimiter.clear();
        }
        else
        {
          info.delimiter.assign(atom->value);
        }
      }
    }

    // Parse mailbox name: "INBOX"
    auto name_result = tokenizer.nextValue();
    if (name_result.has_value())
    {
      if (auto* quoted = std::get_if<parser::Quoted>(&name_result.value()))
      {
        info.name = quoted->value;
      }
      else if (auto* atom = std::get_if<parser::Atom>(&name_result.value()))
      {
        info.name.assign(atom->value);
      }
    }

    return info;
  }

  std::string MailboxInfo::getDecodedName() const
  {
    return aurora::mail::common::utils::decodeImapUtf7(name);
  }

  awaitable<Result<std::vector<MailboxInfo>>> ImapClient::asyncListMailboxes(
      const std::string& reference,
      const std::string& mailbox_pattern)
  {
    log_info(std::format("IMAP: LIST \"{}\" \"{}\"", reference, mailbox_pattern));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::List{ tag, reference, mailbox_pattern });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Validate response status
    auto validated = parseAndValidate(response.value(), StatusType::OK);
    if (!validated.has_value())
    {
      co_return std::unexpected(validated.error());
    }

    // Parse mailbox entries from untagged responses
    std::vector<MailboxInfo> mailboxes;
    std::istringstream stream(response.value());
    std::string line;

    while (std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }

      if (auto mailbox = parseListLine(line))
      {
        mailboxes.push_back(*mailbox);
      }
    }

    log_info(std::format("IMAP: LIST completed - found {} mailboxes", mailboxes.size()));
    co_return mailboxes;
  }

  awaitable<VoidResult> ImapClient::asyncNoop()
  {
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Noop{ tag }, StatusType::OK);
  }

  awaitable<Result<std::string>> ImapClient::asyncUidFetchMail(const std::string& uid_set, const std::string& data_items)
  {
    log_info(std::format("IMAP: UID FETCH {} {}", uid_set, data_items));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::UidFetch{ tag, uid_set, data_items });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: UID FETCH completed");
    co_return response.value();
  }

  awaitable<Result<std::string>> ImapClient::asyncUidSearchMail(const std::string& criteria)
  {
    log_info(std::format("IMAP: UID SEARCH {}", criteria));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::UidSearch{ tag, criteria });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: UID SEARCH completed");
    co_return response.value();
  }

  awaitable<Result<std::string>> ImapClient::asyncUidStoreMail(const std::string& uid_set, const std::string& flags_action)
  {
    log_info(std::format("IMAP: UID STORE {} {}", uid_set, flags_action));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::UidStore{ tag, uid_set, flags_action });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: UID STORE completed");
    co_return response.value();
  }

  awaitable<VoidResult> ImapClient::asyncUidCopyMail(const std::string& uid_set, const std::string& destination_mailbox)
  {
    log_info(std::format("IMAP: UID COPY {} to {}", uid_set, destination_mailbox));

    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::UidCopy{ tag, uid_set, destination_mailbox }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncUidExpunge(const std::string& uid_set)
  {
    log_info(std::format("IMAP: UID EXPUNGE {}", uid_set));

    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::UidExpunge{ tag, uid_set }, StatusType::OK);
  }

  awaitable<Result<std::string>> ImapClient::asyncStatus(const std::string& mailbox, const std::string& status_items)
  {
    log_info(std::format("IMAP: STATUS {} ({})", mailbox, status_items));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::Status{ tag, mailbox, status_items });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: STATUS completed");
    co_return response.value();
  }

  void ImapClient::reset()
  {
    m_current_mailbox.clear();
    m_tag_generator.reset();
    in_idle_ = false;
    idle_tag_.clear();
    capabilities_.reset();
  }

  awaitable<Result<Capabilities>> ImapClient::asyncCapability()
  {
    log_info("IMAP: Requesting server capabilities");

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::Capability{ tag });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Parse capabilities from response
    // Format: * CAPABILITY IMAP4rev1 IDLE CONDSTORE QRESYNC ...
    Capabilities caps;
    std::istringstream stream(response.value());
    std::string line;

    while (std::getline(stream, line))
    {
      if (line.starts_with("* CAPABILITY "))
      {
        std::string cap_line = line.substr(13);  // Skip "* CAPABILITY "
        // Remove trailing \r if present
        if (!cap_line.empty() && cap_line.back() == '\r')
        {
          cap_line.pop_back();
        }

        std::istringstream cap_stream(cap_line);
        std::string cap;
        while (cap_stream >> cap)
        {
          caps.capabilities.insert(cap);
        }
        break;
      }
    }

    capabilities_ = caps;  // Cache for later use
    log_info(std::format("IMAP: Server has {} capabilities", caps.capabilities.size()));
    co_return caps;
  }

  awaitable<VoidResult> ImapClient::asyncIdleStart()
  {
    if (in_idle_)
    {
      co_return std::unexpected(ProtocolError::invalidState("Already in IDLE mode"));
    }

    log_info("IMAP: Entering IDLE mode");

    idle_tag_ = m_tag_generator.next();
    command::Idle idle_cmd{ idle_tag_ };

    // Send IDLE command
    auto serialized = idle_cmd.serialize();
    if (!serialized.has_value())
    {
      co_return std::unexpected(serialized.error());
    }
    auto send_result = co_await writeCommand(serialized.value());
    if (!send_result.has_value())
    {
      co_return std::unexpected(send_result.error());
    }

    // Server should respond with continuation "+ idling" or similar (allow extra untagged noise if stream was briefly
    // misaligned).
    auto cont_result = co_await readResponse([](const std::string& line) { return line.starts_with("+"); }, 32);

    if (!cont_result.has_value())
    {
      co_return std::unexpected(cont_result.error());
    }

    in_idle_ = true;
    log_info("IMAP: Now in IDLE mode, waiting for server notifications");
    co_return VoidResult{};
  }

  awaitable<VoidResult> ImapClient::asyncIdleDone()
  {
    if (!in_idle_)
    {
      co_return std::unexpected(ProtocolError::invalidState("Not in IDLE mode"));
    }

    log_info("IMAP: Exiting IDLE mode");

    // Send DONE (untagged continuation)
    command::IdleDone done_cmd;
    auto serialized = done_cmd.serialize();
    if (!serialized.has_value())
    {
      in_idle_ = false;
      co_return std::unexpected(serialized.error());
    }
    auto send_result = co_await writeCommand(serialized.value());
    if (!send_result.has_value())
    {
      in_idle_ = false;
      co_return std::unexpected(send_result.error());
    }

    // Server responds with tagged OK for the original IDLE command
    auto response = co_await readResponse([this](const std::string& line) { return line.starts_with(idle_tag_); }, 10);

    in_idle_ = false;
    idle_tag_.clear();

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    log_info("IMAP: Exited IDLE mode");
    co_return VoidResult{};
  }

  void ImapClient::cancelIdleWait()
  {
    // Dispatch: see asyncIdleWait — never emit synchronously from arbitrary threads.
    boost::asio::post(
        ioContext(),
        [this]()
        {
          if (auto sig = idle_cancel_outstanding_)
          {
            sig->emit(boost::asio::cancellation_type::terminal);
          }
        });
  }

  awaitable<Result<std::string>> ImapClient::asyncIdleWait(int timeout_seconds)
  {
    if (!in_idle_)
    {
      co_return std::unexpected(ProtocolError::invalidState("Not in IDLE mode - call asyncIdleStart() first"));
    }

    using namespace boost::asio::experimental::awaitable_operators;

    auto executor = co_await boost::asio::this_coro::executor;

    // One cancellation_signal per wait. A long-lived signal + emit after parallel_group
    // tears down the losing branch can double-cancel the same kqueue timer (crash).
    auto cancel_sig = std::make_shared<boost::asio::cancellation_signal>();
    idle_cancel_outstanding_ = cancel_sig;

    // Create a cancellation-aware timeout coroutine
    // This timer can be cancelled externally via cancelIdleWait()
    auto timeout_coro = [timeout_seconds, executor, cancel_sig]() -> awaitable<std::string>
    {
      boost::asio::steady_timer timer(executor, std::chrono::seconds(timeout_seconds > 0 ? timeout_seconds : 1800));

      try
      {
        co_await timer.async_wait(boost::asio::bind_cancellation_slot(cancel_sig->slot(), boost::asio::use_awaitable));
        co_return std::string{};  // Normal timeout
      }
      catch (const boost::system::system_error& e)
      {
        if (e.code() == boost::asio::error::operation_aborted)
        {
          co_return std::string{ "__CANCELLED__" };
        }
        throw;
      }
    };

    // Create the read coroutine - wait for any untagged response
    auto read_coro = [this]() -> awaitable<std::string>
    {
      auto result = co_await readResponse(
          [](const std::string& line)
          {
            // Any untagged response (starts with *) is what we're waiting for
            return line.starts_with("*");
          },
          100);  // max 100 lines - we only need 1 notification

      if (result.has_value())
      {
        co_return result.value();
      }
      co_return std::string{};  // Error case - treat as timeout
    };

    // Race the two operations - whichever finishes first wins
    IdleLongPollReadGuard idle_read_guard(stream());
    std::variant<std::string, std::string> result;
    try
    {
      result = co_await (read_coro() || timeout_coro());
    }
    catch (...)
    {
      idle_cancel_outstanding_.reset();
      throw;
    }

    // Let the losing branch finish reactor teardown before we clear the cancel slot or
    // accept another cancelIdleWait() emit (avoids timer_queue corruption on macOS).
    co_await boost::asio::post(executor, boost::asio::use_awaitable);
    idle_cancel_outstanding_.reset();

    if (result.index() == 0)
    {
      // Read completed first
      auto notification = std::get<0>(result);
      if (!notification.empty())
      {
        co_return notification;
      }
      // Empty notification means read error, treat as timeout
      co_return std::unexpected(ProtocolError::timeout("IDLE wait timed out or read failed"));
    }
    else
    {
      // Timeout or cancellation
      auto timeout_result = std::get<1>(result);
      if (timeout_result == "__CANCELLED__")
      {
        // Explicitly cancelled via cancelIdleWait()
        co_return std::unexpected(ProtocolError::cancelled("IDLE wait cancelled"));
      }
      // Normal timeout
      co_return std::unexpected(ProtocolError::timeout("IDLE wait timed out"));
    }
  }

  // Helper to parse SELECT/EXAMINE response
  static SelectResponse parseSelectResponse(const std::string& raw)
  {
    SelectResponse resp;
    std::istringstream stream(raw);
    std::string line;

    while (std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }

      if (line.find("EXISTS") != std::string::npos)
      {
        // * 172 EXISTS
        std::istringstream ls(line);
        std::string star;
        uint32_t num = 0;
        ls >> star >> num;
        resp.exists = num;
      }
      else if (line.find("RECENT") != std::string::npos)
      {
        // * 1 RECENT
        std::istringstream ls(line);
        std::string star;
        uint32_t num = 0;
        ls >> star >> num;
        resp.recent = num;
      }
      else if (line.find("UIDVALIDITY") != std::string::npos)
      {
        // * OK [UIDVALIDITY 1234567890]
        auto pos = line.find("UIDVALIDITY ");
        if (pos != std::string::npos)
        {
          resp.uidvalidity = std::stoul(line.substr(pos + 12));
        }
      }
      else if (line.find("UIDNEXT") != std::string::npos)
      {
        // * OK [UIDNEXT 4392]
        auto pos = line.find("UIDNEXT ");
        if (pos != std::string::npos)
        {
          resp.uidnext = std::stoul(line.substr(pos + 8));
        }
      }
      else if (line.find("HIGHESTMODSEQ") != std::string::npos)
      {
        // * OK [HIGHESTMODSEQ 715194045007]
        auto pos = line.find("HIGHESTMODSEQ ");
        if (pos != std::string::npos)
        {
          resp.highestmodseq = std::stoull(line.substr(pos + 14));
        }
      }
      else if (line.find("[READ-ONLY]") != std::string::npos)
      {
        resp.read_write = false;
      }
    }

    return resp;
  }

  awaitable<Result<SelectResponse>> ImapClient::asyncSelectCondstore(const std::string& mailbox)
  {
    log_info(std::format("IMAP: SELECT {} (CONDSTORE)", mailbox));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::SelectCondstore{ tag, mailbox });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Validate response status
    auto validated = parseAndValidate(response.value(), StatusType::OK);
    if (!validated.has_value())
    {
      co_return std::unexpected(validated.error());
    }

    m_current_mailbox = mailbox;
    auto parsed = parseSelectResponse(response.value());
    log_info(
        std::format(
            "IMAP: SELECT CONDSTORE completed - {} messages, "
            "uidvalidity={}, highestmodseq={}",
            parsed.exists,
            parsed.uidvalidity,
            parsed.highestmodseq));
    co_return parsed;
  }

  awaitable<Result<SelectResponse>> ImapClient::asyncExamineCondstore(const std::string& mailbox)
  {
    log_info(std::format("IMAP: EXAMINE {} (CONDSTORE)", mailbox));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::ExamineCondstore{ tag, mailbox });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Validate response status
    auto validated = parseAndValidate(response.value(), StatusType::OK);
    if (!validated.has_value())
    {
      co_return std::unexpected(validated.error());
    }

    auto parsed = parseSelectResponse(response.value());
    parsed.read_write = false;
    log_info(std::format("IMAP: EXAMINE CONDSTORE completed - {} messages", parsed.exists));
    co_return parsed;
  }

  awaitable<Result<std::string>>
  ImapClient::asyncUidFetchChangedSince(const std::string& uid_set, uint64_t modseq, const std::string& data_items)
  {
    log_info(std::format("IMAP: UID FETCH {} {} (CHANGEDSINCE {})", uid_set, data_items, modseq));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::UidFetchChangedSince{ tag, uid_set, data_items, modseq });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: UID FETCH CHANGEDSINCE completed");
    co_return response.value();
  }

  awaitable<Result<std::string>>
  ImapClient::asyncUidStoreUnchangedSince(const std::string& uid_set, uint64_t modseq, const std::string& flags_action)
  {
    log_info(std::format("IMAP: UID STORE {} (UNCHANGEDSINCE {}) {}", uid_set, modseq, flags_action));

    std::string tag = m_tag_generator.next();
    auto response =
        co_await sendCommandAndReadResponse(command::UidStoreUnchangedSince{ tag, uid_set, flags_action, modseq });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: UID STORE UNCHANGEDSINCE completed");
    co_return response.value();
  }

  awaitable<VoidResult> ImapClient::asyncEnableQresync()
  {
    log_info("IMAP: Enabling QRESYNC extension");

    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::EnableQresync{ tag }, StatusType::OK);
  }

  awaitable<Result<QresyncResponse>> ImapClient::asyncSelectQresync(
      const std::string& mailbox,
      uint32_t uidvalidity,
      uint64_t modseq,
      const std::string& known_uids)
  {
    log_info(std::format("IMAP: SELECT {} (QRESYNC {} {} {})", mailbox, uidvalidity, modseq, known_uids));

    std::string tag = m_tag_generator.next();
    auto response =
        co_await sendCommandAndReadResponse(command::SelectQresync{ tag, mailbox, uidvalidity, modseq, known_uids });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Validate response status
    auto validated = parseAndValidate(response.value(), StatusType::OK);
    if (!validated.has_value())
    {
      co_return std::unexpected(validated.error());
    }

    m_current_mailbox = mailbox;

    // Parse base SELECT response
    QresyncResponse qresp;
    static_cast<SelectResponse&>(qresp) = parseSelectResponse(response.value());

    // Parse VANISHED responses for expunged UIDs
    // * VANISHED (EARLIER) 41,43:116,118,120:211,214:540
    std::istringstream stream(response.value());
    std::string line;
    while (std::getline(stream, line))
    {
      if (line.find("VANISHED") != std::string::npos)
      {
        // Extract UID list after "VANISHED" or "VANISHED (EARLIER)"
        auto pos = line.find(')');
        if (pos == std::string::npos)
        {
          pos = line.find("VANISHED") + 8;
        }
        else
        {
          pos += 1;
        }
        std::string uids_str = line.substr(pos);

        // Parse comma-separated UIDs/ranges
        std::istringstream uid_stream(uids_str);
        std::string uid_token;
        while (std::getline(uid_stream, uid_token, ','))
        {
          // Handle ranges like "43:116"
          auto colon = uid_token.find(':');
          if (colon != std::string::npos)
          {
            uint32_t start = std::stoul(uid_token.substr(0, colon));
            uint32_t end = std::stoul(uid_token.substr(colon + 1));
            for (uint32_t uid = start; uid <= end; ++uid)
            {
              qresp.expunged_uids.push_back(uid);
            }
          }
          else if (!uid_token.empty())
          {
            // Trim whitespace
            while (!uid_token.empty() && std::isspace(static_cast<unsigned char>(uid_token.front())) != 0)
            {
              uid_token.erase(0, 1);
            }
            while (!uid_token.empty() && std::isspace(static_cast<unsigned char>(uid_token.back())) != 0)
            {
              uid_token.pop_back();
            }
            if (!uid_token.empty())
            {
              qresp.expunged_uids.push_back(std::stoul(uid_token));
            }
          }
        }
      }
    }

    log_info(
        std::format(
            "IMAP: SELECT QRESYNC completed - {} messages, "
            "{} vanished UIDs",
            qresp.exists,
            qresp.expunged_uids.size()));
    co_return qresp;
  }

  awaitable<VoidResult> ImapClient::asyncCreateMailbox(const std::string& mailbox)
  {
    log_info(std::format("IMAP: CREATE \"{}\"", mailbox));
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Create{ tag, mailbox }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncDeleteMailbox(const std::string& mailbox)
  {
    log_info(std::format("IMAP: DELETE \"{}\"", mailbox));
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Delete{ tag, mailbox }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncRenameMailbox(const std::string& old_name, const std::string& new_name)
  {
    log_info(std::format("IMAP: RENAME \"{}\" \"{}\"", old_name, new_name));
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Rename{ tag, old_name, new_name }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncSubscribe(const std::string& mailbox)
  {
    log_info(std::format("IMAP: SUBSCRIBE \"{}\"", mailbox));
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Subscribe{ tag, mailbox }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncUnsubscribe(const std::string& mailbox)
  {
    log_info(std::format("IMAP: UNSUBSCRIBE \"{}\"", mailbox));
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Unsubscribe{ tag, mailbox }, StatusType::OK);
  }

  awaitable<Result<std::vector<MailboxInfo>>> ImapClient::asyncListSubscribed(
      const std::string& reference,
      const std::string& pattern)
  {
    log_info(std::format("IMAP: LSUB \"{}\" \"{}\"", reference, pattern));

    std::string tag = m_tag_generator.next();
    auto response = co_await sendCommandAndReadResponse(command::Lsub{ tag, reference, pattern });

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    // Validate response status
    auto validated = parseAndValidate(response.value(), StatusType::OK);
    if (!validated.has_value())
    {
      co_return std::unexpected(validated.error());
    }

    // Parse mailbox entries
    std::vector<MailboxInfo> mailboxes;
    std::istringstream stream(response.value());
    std::string line;

    while (std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      // LSUB response format is same as LIST
      if (line.starts_with("* LSUB "))
      {
        line = "* LIST " + line.substr(7);
      }
      if (auto mailbox = parseListLine(line))
      {
        mailboxes.push_back(*mailbox);
      }
    }

    log_info(std::format("IMAP: LSUB completed - found {} subscribed mailboxes", mailboxes.size()));
    co_return mailboxes;
  }

  awaitable<VoidResult> ImapClient::asyncAppend(
      const std::string& mailbox,
      const std::string& message,
      const std::string& flags,
      const std::string& date_time)
  {
    log_info(std::format("IMAP: APPEND to \"{}\" ({} bytes)", mailbox, message.size()));

    std::string tag = m_tag_generator.next();
    command::Append append_cmd{ tag, mailbox, flags, date_time, message };

    // Send the APPEND command (with literal size)
    auto serialized = append_cmd.serialize();
    if (!serialized.has_value())
    {
      co_return std::unexpected(serialized.error());
    }
    auto send_result = co_await writeCommand(serialized.value());
    if (!send_result.has_value())
    {
      co_return std::unexpected(send_result.error());
    }

    // Server should respond with continuation "+" ready for literal
    auto cont_result = co_await readResponse([](const std::string& line) { return line.starts_with("+"); }, 10);

    if (!cont_result.has_value())
    {
      co_return std::unexpected(ProtocolError::protocol("Server rejected APPEND literal"));
    }

    // Send the actual message content
    auto msg_result = co_await writeCommand(message + "\r\n");
    if (!msg_result.has_value())
    {
      co_return std::unexpected(msg_result.error());
    }

    // Read final response
    auto response = co_await readResponse(
        [&tag](const std::string& line) { return line.starts_with(tag); },
        60);  // Allow more time for large uploads

    if (!response.has_value())
    {
      co_return std::unexpected(response.error());
    }

    auto parsed = parseAndValidate(response.value(), StatusType::OK);
    if (!parsed.has_value())
    {
      co_return std::unexpected(parsed.error());
    }

    log_info("IMAP: APPEND completed");
    co_return VoidResult{};
  }

  awaitable<VoidResult> ImapClient::asyncUidMove(const std::string& uid_set, const std::string& destination_mailbox)
  {
    log_info(std::format("IMAP: UID MOVE {} to \"{}\"", uid_set, destination_mailbox));

    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::UidMove{ tag, uid_set, destination_mailbox }, StatusType::OK);
  }

  awaitable<VoidResult> ImapClient::asyncClose()
  {
    log_info("IMAP: CLOSE");
    std::string tag = m_tag_generator.next();
    auto result = co_await sendCommandAndValidate(command::Close{ tag }, StatusType::OK);

    if (result.has_value())
    {
      m_current_mailbox.clear();
    }

    co_return result;
  }

  awaitable<VoidResult> ImapClient::asyncExpunge()
  {
    log_info("IMAP: EXPUNGE");
    std::string tag = m_tag_generator.next();
    co_return co_await sendCommandAndValidate(command::Expunge{ tag }, StatusType::OK);
  }

}  // namespace aurora::mail::imap
