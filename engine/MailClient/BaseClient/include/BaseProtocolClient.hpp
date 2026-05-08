#ifndef BASE_PROTOCOL_CLIENT_HPP
#define BASE_PROTOCOL_CLIENT_HPP

#include <FunctionRef.hpp>
#include <ProtocolError.hpp>
#include <StartupConfig.hpp>
#include <Stream.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace aurora::mail::common
{

  // Use the unified ConnectionMode from config
  using config::ConnectionMode;

  namespace asio = boost::asio;
  using asio::awaitable;
  using boost::system::error_code;
  using common::stream::PlainStream;
  using common::stream::SecureStream;
  using common::stream::Stream;
  using common::stream::TimedOutStream;

  /**
   * @brief Mail protocol family this client speaks.
   *
   * Plain @c enum @c class — earlier revisions wrapped this in a struct with
   * a member @c toString(); that buys nothing now that the only piece of
   * derived data (the display string) is provided as a free function below.
   */
  enum class MailProtocol : uint8_t
  {
    SMTP,
    IMAP
  };

  /**
   * @brief Human-readable protocol name suitable for logging.
   *
   * Returns a static string view; the lifetime is independent of the
   * argument so callers may safely store the result.
   */
  [[nodiscard]] constexpr std::string_view toString(MailProtocol p) noexcept
  {
    return p == MailProtocol::SMTP ? std::string_view{ "SMTP" } : std::string_view{ "IMAP" };
  }

  /**
   * @brief Coarse-grained transport state of a @c BaseProtocolClient.
   *
   * Today the public surface only differentiates "connected vs not". The
   * @c TcpConnected / @c TlsActive split is internal book-keeping so that
   * future failure-recovery flows (e.g. re-handshake on a hung TLS session,
   * or "STARTTLS already negotiated" detection) do not have to reintroduce
   * a second flag. @c isConnected() collapses both connected sub-states
   * back into a single bool to preserve the existing public contract.
   */
  enum class ConnectionState : uint8_t
  {
    Disconnected = 0,
    TcpConnected = 1,
    TlsActive = 2,
  };

  /**
   * @brief Base class for protocol clients (SMTP, IMAP, POP3, etc.)
   *
   * Provides unified implementations for:
   * - Stream management (plain, secure, timed-out)
   * - Reading multi-line responses with protocol-specific predicates
   * - Writing commands with error handling
   * - TLS/SSL upgrade logic
   * - Connection state tracking
   *
   * Subclasses implement protocol-specific logic by providing:
   * - Response parsing logic
   * - Command structures
   * - State machines (if needed)
   */
  class BaseProtocolClient
  {
   public:
    BaseProtocolClient(
        asio::io_context& io_context,
        asio::ssl::context& ssl_context,
        int default_timeout,
        MailProtocol protocol);

    virtual ~BaseProtocolClient() = default;

    // Non-copyable and non-movable. The class holds references to externally owned
    // io_context/ssl_context, so moving would silently rebind those references and
    // give the misleading impression that the contexts travel with the object.
    BaseProtocolClient(const BaseProtocolClient&) = delete;
    BaseProtocolClient& operator=(const BaseProtocolClient&) = delete;
    BaseProtocolClient(BaseProtocolClient&&) = delete;
    BaseProtocolClient& operator=(BaseProtocolClient&&) = delete;

    /**
     * @brief Check if connection is currently active.
     *
     * Returns true once the TCP handshake (and TLS handshake, if applicable)
     * has completed successfully and false thereafter once @c closeConnection
     * has run. The internal @c ConnectionState distinguishes plain TCP from
     * an active TLS session, but the public API intentionally collapses
     * those into a single boolean — external code only ever needs to know
     * whether the transport is usable at all.
     */
    [[nodiscard]] bool isConnected() const noexcept
    {
      return connection_state_ != ConnectionState::Disconnected;
    }

    /**
     * @brief Close the transport (TCP/TLS). Idempotent; safe if never connected.
     */
    awaitable<VoidResult> closeConnection();

   protected:
    /**
     * @brief Write data to the stream.
     *
     * @param data Data to write (should include protocol line endings).
     *
     * MUST be taken by value, not by reference. C++ coroutines do NOT
     * extend the lifetime of reference parameters: after the first
     * @c co_await, the original argument may be destroyed, leaving the
     * frame's reference dangling. @c asio::buffer(data) below would then
     * point at freed memory and we'd corrupt the result handed back to
     * the caller (we used to crash inside @c boost::system::error_code::message
     * on access-violation due to exactly this — "garbage error_code" with
     * a torn @c category pointer was actually torn @c data text).
     *
     * @return VoidResult indicating success or error
     */
    awaitable<VoidResult> writeCommand(std::string data);

    /**
     * @brief Read a multi-line response from the stream.
     *
     * Uses a predicate to determine when the response is complete.
     * Calls detectLiteralSize() to allow protocol-specific literal handling.
     *
     * Output format (NOT a verbatim wire copy):
     * - Lines (delimited by CRLF on the wire) are joined with a single "\r\n".
     * - When detectLiteralSize() identifies an IMAP {N} literal, exactly N bytes
     *   are read and inserted into the buffer prefixed by an additional "\r\n".
     * - Subsequent text after the literal is again CRLF-prefixed when appended,
     *   even though no CRLF separates the literal payload from the following
     *   text on the wire. Downstream parsers must treat the result as a
     *   normalized representation rather than a verbatim transcript.
     *
     * @param is_final_line Predicate returning true for the final line
     * @param max_lines Maximum lines to read (safety limit)
     * @param max_total_bytes Maximum total bytes to accumulate (safety limit;
     *        guards against pathological literals or runaway responses)
     * @return Result containing the complete response or error
     */
    awaitable<Result<std::string>> readResponse(
        FunctionRef<bool(const std::string&)> is_final_line,
        std::size_t max_lines = 1'000'000,                                // 1M lines for large mailboxes
        std::size_t max_total_bytes = std::size_t{ 256 } * 1024 * 1024);  // 256 MiB hard cap

    /**
     * @brief Detect if a line indicates incoming literal data.
     *
     * Override in derived classes for protocol-specific literal handling.
     * For example, IMAP uses {size} syntax for literals.
     *
     * @param line The line to check for literal indicator
     * @return The literal size if detected, std::nullopt otherwise
     */
    virtual std::optional<std::size_t> detectLiteralSize(std::string_view) const
    {
      // Base implementation: no literal handling
      // Override in derived classes for protocol-specific literal syntax
      return std::nullopt;
    }

    /**
     * @brief Combined write-then-read operation.
     *
     * This is the most common pattern in protocol clients:
     * 1. Send a command
     * 2. Read the response
     * 3. Return the response for parsing
     *
     * @param command Command string to send
     * @param is_final_line Predicate for detecting response completion
     * @param max_lines Maximum response lines
     * @return Result containing the complete response or error
     */
    awaitable<Result<std::string>> sendCommandAndReadResponse(
        const std::string& command,
        FunctionRef<bool(const std::string&)> is_final_line,
        std::size_t max_lines = 1'000'000);

    // -- Connection Management -- //

    /**
     * @brief Establish TCP connection to server.
     *
     * For implicit TLS (SMTPS/IMAPS port 465/993), set secure=true.
     * For STARTTLS, set secure=false, then call upgradeToTLS() after
     * protocol-specific negotiation (EHLO + STARTTLS command for SMTP,
     * CAPABILITY + STARTTLS for IMAP).
     *
     * @param hostname Server hostname or IP
     * @param port Server port
     * @param secure If true, perform TLS handshake immediately after TCP connect
     * @return VoidResult indicating success or error
     */
    awaitable<VoidResult> establishConnection(const std::string& hostname, uint16_t port, bool secure = false);

    /**
     * @brief Upgrade current connection to TLS.
     *
     * Used for STARTTLS flows. The stream must currently be PlainStream.
     *
     * @return VoidResult indicating success or error
     */
    awaitable<VoidResult> upgradeToTLS();

    // -- Accessors for derived classes -- //
    //
    // Naming follows the project's preference for noun-style accessors on
    // value-returning members; the legacy @c getXxx() spellings were renamed
    // wholesale (see review note 18). External consumers (@c ImapClient,
    // @c SmtpClient, the @c BaseProtocolClient test suite) were updated in
    // the same change set.

    [[nodiscard]] asio::io_context& ioContext() noexcept
    {
      return io_context_;
    }
    [[nodiscard]] asio::ssl::context& sslContext() noexcept
    {
      return ssl_context_;
    }
    [[nodiscard]] std::string_view protocolName() const noexcept
    {
      return toString(mail_protocol_);
    }
    [[nodiscard]] const std::string& serverHostname() const noexcept
    {
      return server_hostname_;
    }

    // Stream access (for derived classes that need direct access).
    // Returns the concrete TimedOutStream the base class always owns; this is
    // a deliberate part of the type contract, so callers no longer need to
    // dynamic_cast a base Stream* to recover the timeout decorator.
    [[nodiscard]] TimedOutStream* stream() noexcept
    {
      return stream_.get();
    }

   private:
    asio::io_context& io_context_;
    asio::ssl::context& ssl_context_;

    // Class invariant: always non-null after construction. The decorator wraps
    // either a PlainStream (initial / post-close / pre-STARTTLS) or a
    // SecureStream (after upgradeToTLS / implicit-TLS connect)
    std::unique_ptr<TimedOutStream> stream_;
    MailProtocol mail_protocol_;
    std::string server_hostname_;

    std::chrono::seconds timeout_;
    ConnectionState connection_state_ = ConnectionState::Disconnected;
  };

}  // namespace aurora::mail::common

#endif  // BASE_PROTOCOL_CLIENT_HPP
