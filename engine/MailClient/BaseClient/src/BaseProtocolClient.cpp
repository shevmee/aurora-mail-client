#include <BaseProtocolClient.hpp>
#include <LoggerInstance.hpp>
#include <algorithm>
#include <cctype>
#include <expected>
#include <format>
#include <string_view>

#include "ProtocolError.hpp"

namespace aurora::mail::common
{
  namespace
  {
    constexpr std::size_t kMaxServerLogChars = 512;

    bool looksLikeBase64CredentialLine(std::string_view line)
    {
      if (line.size() < 48 || line.size() > 65536)
      {
        return false;
      }
      return std::all_of(
          line.begin(), line.end(), [](unsigned char c) { return std::isalnum(c) || c == '+' || c == '/' || c == '='; });
    }

    /** Never log OAuth tokens, SASL payloads, plaintext IMAP LOGIN passwords,
     *  or any base64 SASL continuations. */
    std::string redactClientLineForLog(std::string_view line)
    {
      // Trim any trailing CR/LF for matching, but preserve original length for logs.
      std::string_view sv(line);
      while (!sv.empty() && (sv.back() == '\r' || sv.back() == '\n'))
      {
        sv.remove_suffix(1);
      }

      // SMTP: "AUTH XOAUTH2 <token>" — covered explicitly to avoid leaking the token
      // even on shorter lines that wouldn't trip the base64 heuristic.
      constexpr std::string_view smtpXoauth = "AUTH XOAUTH2 ";
      if (sv.size() > smtpXoauth.size() && sv.compare(0, smtpXoauth.size(), smtpXoauth) == 0)
      {
        return std::string(smtpXoauth) + "<redacted>";
      }

      // SMTP: "AUTH PLAIN <base64(\\0user\\0password)>" — single-shot SASL PLAIN.
      constexpr std::string_view smtpAuthPlain = "AUTH PLAIN ";
      if (sv.size() > smtpAuthPlain.size() && sv.compare(0, smtpAuthPlain.size(), smtpAuthPlain) == 0)
      {
        return std::string(smtpAuthPlain) + "<redacted>";
      }

      // IMAP: "<TAG> AUTHENTICATE PLAIN <base64>" — single-shot SASL PLAIN.
      // IMAP: "<TAG> AUTHENTICATE XOAUTH2 <base64>" — single-shot XOAUTH2.
      // IMAP: "<TAG> LOGIN \"user\" \"password\"" — cleartext LOGIN.
      // We locate the verb after the tag (first whitespace) and redact accordingly.
      if (const auto firstSpace = sv.find(' '); firstSpace != std::string_view::npos && firstSpace > 0)
      {
        const std::string_view rest = sv.substr(firstSpace + 1);
        constexpr std::string_view imapAuthPrefix = "AUTHENTICATE ";
        constexpr std::string_view imapLoginPrefix = "LOGIN";
        if (rest.size() > imapAuthPrefix.size() && rest.compare(0, imapAuthPrefix.size(), imapAuthPrefix) == 0)
        {
          // Preserve "<TAG> AUTHENTICATE <MECHANISM>" so debug logs still show
          // which mechanism was attempted; redact any payload after it.
          const auto mechStart = firstSpace + 1 + imapAuthPrefix.size();
          const auto mechEnd = sv.find(' ', mechStart);
          if (mechEnd == std::string_view::npos)
          {
            return std::string(sv);  // No payload on the line.
          }
          return std::string(sv.substr(0, mechEnd)) + " <redacted>";
        }
        if (rest.size() >= imapLoginPrefix.size() && rest.compare(0, imapLoginPrefix.size(), imapLoginPrefix) == 0 &&
            (rest.size() == imapLoginPrefix.size() || rest[imapLoginPrefix.size()] == ' '))
        {
          // "<TAG> LOGIN ..." — redact everything after the verb to avoid logging
          // the password (and to avoid logging the username, which is also PII).
          return std::string(sv.substr(0, firstSpace + 1)) + "LOGIN <redacted>";
        }
      }

      // Catch-all for unsolicited base64 continuations (e.g., the username and
      // password lines that follow a server "334" challenge in SMTP AUTH LOGIN).
      if (looksLikeBase64CredentialLine(sv))
      {
        return "<base64 credential line redacted>";
      }
      return std::string(sv);
    }

    /** Mirror of redactClientLineForLog for server-emitted lines.
     *
     *  Servers participating in SASL exchanges echo content that is just as
     *  sensitive as what the client sends:
     *   - SMTP "334 <base64>" continuations are username/password prompts; the
     *     payload itself is a fixed prompt in vanilla AUTH LOGIN, but real
     *     deployments occasionally embed identifiers, and AUTH XOAUTH2 error
     *     continuations carry JSON with reason codes that include the user.
     *   - IMAP "+ <base64>" continuations are equivalent.
     *   - SMTP/IMAP authentication failure replies frequently quote the
     *     submitted username back at the client (e.g. Gmail's
     *     "5.7.8 Username and Password not accepted ... user@example.com").
     *
     *  We redact conservatively: keep the protocol-level prefix that is needed
     *  for debugging the conversation flow, drop the payload that carries PII
     *  or credential material. Anything that does not match a known auth
     *  pattern is returned unchanged.
     */
    std::string redactServerLineForLog(std::string_view line)
    {
      std::string_view sv = line;
      while (!sv.empty() && (sv.back() == '\r' || sv.back() == '\n'))
      {
        sv.remove_suffix(1);
      }

      // IMAP continuation: "+ <payload>" or just "+".
      if (sv.size() >= 1 && sv.front() == '+' && (sv.size() == 1 || sv[1] == ' '))
      {
        return "+ <redacted continuation>";
      }

      // SMTP continuation: "334 <base64-prompt>" (and "334-" for multi-line).
      // The base64 is a fixed prompt for AUTH LOGIN but variable for XOAUTH2,
      // and for some deployments may carry identifiers. Always redact.
      if (sv.size() >= 4 && (sv.compare(0, 4, "334 ") == 0 || sv.compare(0, 4, "334-") == 0))
      {
        return std::string(sv.substr(0, 4)) + "<redacted continuation>";
      }

      // SMTP authentication errors. We can't tell from the line alone whether
      // we're inside an AUTH exchange, but lines that quote what looks like an
      // email address are virtually always credential-related and worth
      // sanitizing. Keep the SMTP reply code (first 3 digits + separator) so
      // operators still see whether we got 535 vs 454 vs 503, etc.
      if (sv.size() >= 4 && std::isdigit(static_cast<unsigned char>(sv[0])) != 0 &&
          std::isdigit(static_cast<unsigned char>(sv[1])) != 0 && std::isdigit(static_cast<unsigned char>(sv[2])) != 0 &&
          (sv[3] == ' ' || sv[3] == '-') && sv.find('@') != std::string_view::npos)
      {
        return std::string(sv.substr(0, 4)) + "<redacted: contains addr-like token>";
      }

      // IMAP tagged authentication results frequently echo the username in
      // error text, e.g. "A001 NO [AUTHENTICATIONFAILED] ... user@example.com".
      // Same heuristic: if the line carries an "@" we strip everything past
      // the IMAP status word to keep tag + status visible.
      if (const auto firstSpace = sv.find(' '); firstSpace != std::string_view::npos && firstSpace > 0)
      {
        const std::string_view tag = sv.substr(0, firstSpace);
        const std::string_view rest = sv.substr(firstSpace + 1);
        // Heuristic: tag is alphanumeric and short (<= 16 chars) for IMAP.
        const bool looksLikeImapTag = tag.size() <= 16 && !tag.empty() &&
                                      std::all_of(tag.begin(), tag.end(), [](unsigned char c) { return std::isalnum(c); });
        if (looksLikeImapTag && rest.find('@') != std::string_view::npos &&
            (rest.starts_with("NO ") || rest.starts_with("BAD ") || rest.starts_with("NO[") || rest.starts_with("BAD[")))
        {
          const auto statusEnd = rest.find(' ');
          const std::string_view statusWord = statusEnd == std::string_view::npos ? rest : rest.substr(0, statusEnd);
          return std::string(tag) + " " + std::string(statusWord) + " <redacted: contains addr-like token>";
        }
      }

      return std::string(sv);
    }

    std::string truncateForServerLog(std::string_view raw)
    {
      if (raw.size() <= kMaxServerLogChars)
      {
        return std::string(raw);
      }
      return std::format("{} ... [truncated, {} bytes total]", std::string(raw.substr(0, 240)), raw.size());
    }

    /** Apply server-side redaction line-by-line on a CRLF-joined buffer.
     *  Truncation is then applied to the (possibly redacted) result. */
    std::string sanitizeServerResponseForLog(std::string_view raw)
    {
      std::string out;
      out.reserve(raw.size());
      std::size_t i = 0;
      while (i < raw.size())
      {
        const std::size_t crlf = raw.find("\r\n", i);
        const std::size_t end = crlf == std::string_view::npos ? raw.size() : crlf;
        out.append(redactServerLineForLog(raw.substr(i, end - i)));
        if (crlf == std::string_view::npos)
        {
          break;
        }
        out.append("\r\n");
        i = crlf + 2;
      }
      return truncateForServerLog(out);
    }
  }  // namespace

  BaseProtocolClient::BaseProtocolClient(
      asio::io_context& io_context,
      asio::ssl::context& ssl_context,
      int default_timeout,
      MailProtocol protocol)
      : io_context_(io_context),
        ssl_context_(ssl_context),
        mail_protocol_(protocol),
        timeout_(default_timeout)
  {
    // Create initial plain stream wrapped with timeout
    auto plain_stream = std::make_unique<PlainStream>(io_context.get_executor());
    stream_ = std::make_unique<TimedOutStream>(std::move(plain_stream), timeout_);
  }

  awaitable<VoidResult> BaseProtocolClient::writeCommand(std::string data)
  {
    // NOTE: `data` is intentionally taken by value — see header. The buffer
    // produced by `asio::buffer(data)` below holds a raw pointer into this
    // local's storage; after the `co_await` the storage MUST still be alive,
    // which it is exactly because we own `data` here.
    const std::size_t end = data.find('\r');
    const std::string_view lineSv(data.data(), end == std::string::npos ? data.size() : end);
    log_debug(std::format("{} C: {}", toString(mail_protocol_), redactClientLineForLog(lineSv)));

    auto write_result = co_await stream_->write(asio::buffer(data));
    if (auto ec = write_result.error(); ec.failed())
    {
      auto err = ProtocolError::io("Write failed", ec.message());
      log_error(err.toString());
      co_return std::unexpected(err);
    }

    co_return VoidResult{};
  }

  awaitable<Result<std::string>> BaseProtocolClient::readResponse(
      FunctionRef<bool(const std::string&)> is_final_line,
      std::size_t max_lines,
      std::size_t max_total_bytes)
  {
    std::string complete_response;
    std::string line;
    std::size_t line_count = 0;

    // Rejects any append (line, CRLF separator, or literal payload) that would
    // push the accumulated response past max_total_bytes. The line-count limit
    // alone is not a useful memory guard because a single legitimate IMAP
    // literal may be hundreds of MB (large attachments) and a malicious one
    // could announce arbitrary sizes.
    auto would_overflow = [&](std::size_t additional) { return additional > max_total_bytes - complete_response.size(); };

    while (line_count < max_lines)
    {
      line_count++;

      auto result = co_await stream_->readUntil(line, "\r\n");
      if (auto ec = result.error(); ec.failed())
      {
        // Expected when a coroutine read is cancelled (e.g. leaving IMAP IDLE).
        if (ec == asio::error::operation_aborted)
        {
          log_debug(std::format("{}: Read cancelled ({})", toString(mail_protocol_), ec.message()));
        }
        else
        {
          auto err = ProtocolError::io("Read failed", ec.message());
          log_error(err.toString());
        }
        co_return std::unexpected(ProtocolError::io("Read failed", ec.message()));
      }

      const std::size_t separator_bytes = complete_response.empty() ? 0 : 2;
      if (would_overflow(separator_bytes + line.size()))
      {
        auto err = ProtocolError::protocol(std::format("Response exceeded maximum {} bytes", max_total_bytes));
        log_error(err.toString());
        co_return std::unexpected(err);
      }
      if (!complete_response.empty())
      {
        complete_response.append("\r\n");
      }
      complete_response.append(line);

      // Check for protocol-specific literal data (e.g., IMAP's {size} syntax)
      if (auto literal_size = detectLiteralSize(line))
      {
        // Refuse before allocating: an unbounded resize() on attacker-controlled
        // size would otherwise be exploitable as a memory-exhaustion vector.
        if (would_overflow(2 + *literal_size))
        {
          auto err = ProtocolError::protocol(
              std::format("Literal of {} bytes would exceed response size cap ({} bytes)", *literal_size, max_total_bytes));
          log_error(err.toString());
          co_return std::unexpected(err);
        }

        log_debug(std::format("{}: Reading literal of {} bytes", toString(mail_protocol_), *literal_size));

        // Read exactly literal_size bytes (may contain \r\n!)
        std::string literal_data;
        literal_data.resize(*literal_size);

        auto literal_result = co_await stream_->readExactly(asio::buffer(literal_data.data(), *literal_size));

        if (auto ec = literal_result.error(); ec.failed())
        {
          if (ec == asio::error::operation_aborted)
          {
            log_debug(std::format("{}: Literal read cancelled ({})", toString(mail_protocol_), ec.message()));
          }
          else
          {
            auto err =
                ProtocolError::io(std::format("Literal read failed (expected {} bytes)", *literal_size), ec.message());
            log_error(err.toString());
          }
          co_return std::unexpected(
              ProtocolError::io(std::format("Literal read failed (expected {} bytes)", *literal_size), ec.message()));
        }

        // Append literal data. The leading "\r\n" is a deliberate part of the
        // normalized output format (see readResponse() doc); it does not
        // appear on the wire between the {N} marker and the literal payload.
        complete_response.append("\r\n");
        complete_response.append(literal_data);

        // Continue reading (might be closing paren, more literals, etc.)
        line.clear();
        continue;
      }

      // Check if this is the final line
      if (is_final_line(line))
      {
        break;
      }

      line.clear();
    }

    if (line_count >= max_lines)
    {
      auto err = ProtocolError::protocol(std::format("Response exceeded maximum {} lines", max_lines));
      log_error(err.toString());
      co_return std::unexpected(err);
    }

    // Log the complete multi-line response. Redact known auth-related patterns
    // (SASL continuations and authentication failure lines that echo the
    // submitted username) before flattening CRLF -> LF for readability. The
    // raw response is still returned to callers untouched; only the log copy
    // is sanitized.
    std::string log_response = sanitizeServerResponseForLog(complete_response);
    std::size_t pos = 0;
    while ((pos = log_response.find("\r\n", pos)) != std::string::npos)
    {
      log_response.replace(pos, 2, "\n");
      pos += 1;
    }
    if (!log_response.empty() && log_response.back() == '\n')
    {
      log_response.pop_back();
    }
    log_debug(std::format("{} S:{}", toString(mail_protocol_), log_response));

    co_return complete_response;
  }

  awaitable<Result<std::string>> BaseProtocolClient::sendCommandAndReadResponse(
      const std::string& command,
      FunctionRef<bool(const std::string&)> is_final_line,
      std::size_t max_lines)
  {
    auto write_result = co_await writeCommand(command);
    if (!write_result.has_value())
    {
      co_return std::unexpected(write_result.error());
    }

    co_return co_await readResponse(is_final_line, max_lines);
  }

  awaitable<VoidResult> BaseProtocolClient::establishConnection(const std::string& hostname, uint16_t port, bool secure)
  {
    using asio::ip::tcp;

    // Fail fast on reconnect-without-close. The internal stream_ is freshly
    // rebuilt only by the constructor and closeConnection(); without this guard,
    // a second establishConnection() call would invoke connect() on a stream
    // whose underlying socket is already attached to a peer, which has no
    // well-defined contract here. Callers are expected to closeConnection()
    // before reconnecting.
    if (connection_state_ != ConnectionState::Disconnected)
    {
      auto err = ProtocolError::connection(
          std::format("Connection already established to {}", server_hostname_),
          "call closeConnection() before reconnecting");
      log_error(err.toString());
      co_return std::unexpected(err);
    }

    server_hostname_ = hostname;

    log_info(std::format("{}: Connecting to {}:{} (secure: {})", toString(mail_protocol_), hostname, port, secure));

    // Resolve hostname
    tcp::resolver resolver(io_context_);
    error_code resolve_ec;
    auto endpoints_result = co_await resolver.async_resolve(
        hostname, std::to_string(port), asio::redirect_error(asio::use_awaitable, resolve_ec));

    if (resolve_ec)
    {
      auto err = ProtocolError::connection(std::format("Failed to resolve {}:{}", hostname, port), resolve_ec.message());
      log_error(err.toString());
      co_return std::unexpected(err);
    }

    // Establish TCP connection.
    //
    // Two important deviations from "naively await stream_->connect for every
    // endpoint":
    //
    // 1. Per-attempt connect timeout is much shorter than timeout_.
    //    timeout_ governs slow-but-progressing operations (TLS handshake,
    //    Gmail responses, IDLE keep-alive). For TCP connect a single failing
    //    endpoint should fail fast so we can fall over to the next address
    //    quickly. The OS already has its own connect timeout (~75s on macOS),
    //    so without an explicit short timer here a host with two unreachable
    //    IPv6 endpoints leaves the user staring at the screen for a full
    //    timeout_ period per address before IPv4 is even tried.
    //
    // 2. We rebuild stream_ between failed attempts. TimedOutStream::connect
    //    races async_connect against a steady_timer via experimental::
    //    awaitable_operators::||; on timeout, depending on Boost version and
    //    cancellation propagation, the loser is not guaranteed to fully
    //    unwind synchronously. Reusing the same socket on the next iteration
    //    then trips POSIX EALREADY ("Operation already in progress").
    //    Destroying the stream closes the underlying socket, which is the
    //    cheapest reliable way to cancel any stragglers and start clean.
    constexpr auto connect_attempt_timeout = std::chrono::seconds(2);
    stream_->set_timeout(connect_attempt_timeout);

    error_code ec;
    bool connected = false;
    for (const auto& ep : endpoints_result)
    {
      ec = co_await stream_->connect(ep);
      if (!ec.failed())
      {
        log_info(
            std::format(
                "{}: TCP connected to {}:{}",
                toString(mail_protocol_),
                ep.endpoint().address().to_string(),
                ep.endpoint().port()));
        connected = true;
        break;
      }

      log_warn(
          std::format(
              "{}: TCP connect to {}:{} failed: {}; trying next endpoint",
              toString(mail_protocol_),
              ep.endpoint().address().to_string(),
              ep.endpoint().port(),
              ec.message()));

      auto fresh_plain = std::make_unique<PlainStream>(io_context_.get_executor());
      stream_ = std::make_unique<TimedOutStream>(std::move(fresh_plain), connect_attempt_timeout);
    }

    // Restore the regular operation timeout for everything past TCP connect
    // (greeting read, STARTTLS exchange, TLS handshake, application traffic).
    // Done unconditionally so a failure path also leaves stream_ in the
    // expected "long-timeout" state for any subsequent reuse / inspection.
    stream_->set_timeout(timeout_);

    if (!connected)
    {
      auto err = ProtocolError::connection(std::format("Failed to connect to {}:{}", hostname, port), ec.message());
      log_error(err.toString());
      co_return std::unexpected(err);
    }

    // We are now reachable on plain TCP. Record the state before any TLS
    // upgrade so that a failed handshake leaves us in a state that
    // closeConnection() will still clean up correctly.
    connection_state_ = ConnectionState::TcpConnected;

    // For implicit TLS (SMTPS/IMAPS), upgrade immediately
    if (secure)
    {
      auto upgrade_result = co_await upgradeToTLS();
      if (!upgrade_result)
      {
        co_return upgrade_result;
      }
    }

    log_info(std::format("{}: Connection established", toString(mail_protocol_)));
    co_return VoidResult{};
  }

  awaitable<VoidResult> BaseProtocolClient::upgradeToTLS()
  {
    log_info(std::format("{}: Upgrading connection to TLS", toString(mail_protocol_)));

    // stream_ is typed as TimedOutStream by class invariant, so no downcast is
    // required. extract_inner_as<PlainStream>() will report TypeMismatch if the
    // decorator currently wraps something other than a PlainStream (e.g. a
    // double STARTTLS upgrade attempt), which is the only failure mode the
    // previous static_cast guard pretended to detect.
    auto plain_stream_result = stream_->extract_inner_as<PlainStream>();
    if (!plain_stream_result)
    {
      auto err = ProtocolError::tls("Failed to extract plain stream", plain_stream_result.error().message());
      log_error(err.toString());
      co_return std::unexpected(err);
    }
    auto secure_stream = std::make_unique<SecureStream>(std::move(*plain_stream_result.value()), ssl_context_);

    // Set SNI hostname for certificate verification
    secure_stream->setServerHostname(server_hostname_);

    // Perform SSL handshake
    auto ec = co_await secure_stream->handshake();
    if (ec)
    {
      auto err = ProtocolError::tls("SSL handshake failed", ec.message());
      log_error(err.toString());
      co_return std::unexpected(err);
    }

    // Wrap back in timeout decorator
    stream_ = std::make_unique<TimedOutStream>(std::move(secure_stream), timeout_);

    // Promote to TlsActive only after every byte of the handshake has settled:
    // a failure above leaves connection_state_ at TcpConnected, which is the
    // truthful state for whatever recovery the caller chooses (most clients
    // call closeConnection() and propagate the error).
    connection_state_ = ConnectionState::TlsActive;

    log_info(std::format("{}: TLS upgrade completed", toString(mail_protocol_)));
    co_return VoidResult{};
  }

  awaitable<VoidResult> BaseProtocolClient::closeConnection()
  {
    if (connection_state_ != ConnectionState::Disconnected)
    {
      log_info(std::format("{}: Closing connection", toString(mail_protocol_)));

      // stream_ is non-null by class invariant: the constructor builds it and
      // every closeConnection() rebuilds it before returning, so there is no
      // observable state in which it can be missing.
      auto ec = co_await stream_->disconnect();
      if (ec.failed())
      {
        log_warn(ProtocolError::io("Failed to close connection", ec.message()).toString());
        // Don't fail, just warn
      }

      log_info(std::format("{}: Connection closed", toString(mail_protocol_)));
    }

    connection_state_ = ConnectionState::Disconnected;

    // After TLS, stream_ wraps SecureStream. The next implicit-TLS connect must start from PlainStream
    // again (see establishConnection / upgradeToTLS). Rebuild the same stack as in the constructor.
    auto plain_stream = std::make_unique<PlainStream>(io_context_.get_executor());
    stream_ = std::make_unique<TimedOutStream>(std::move(plain_stream), timeout_);

    co_return VoidResult{};
  }

}  // namespace aurora::mail::common
