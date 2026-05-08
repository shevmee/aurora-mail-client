#ifndef IMAP_CLIENT_HPP
#define IMAP_CLIENT_HPP

#include <BaseProtocolClient.hpp>
#include <LoggerInstance.hpp>
#include <ProtocolError.hpp>
#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include <boost/asio/cancellation_signal.hpp>

#include "ImapCommand.hpp"
#include "ImapResponse.hpp"
#include "TagGenerator.hpp"

namespace aurora::mail::imap
{

  /**
   * @brief Represents a mailbox (folder) from IMAP LIST/LSUB response
   *
   * Example LIST response: * LIST (\HasNoChildren) "/" "INBOX"
   */
  struct MailboxInfo
  {
    std::vector<std::string> attributes;  ///< Flags like \HasNoChildren, \Noselect, etc.
    std::string delimiter;                ///< Hierarchy delimiter (usually "/" or ".")
    std::string name;                     ///< Mailbox name in IMAP UTF-7 encoding (for protocol use)

    /**
     * @brief Decode IMAP Modified UTF-7 mailbox name to UTF-8
     *
     * IMAP uses Modified UTF-7 (RFC 3501) for non-ASCII characters.
     *
     * @return Decoded UTF-8 name for display purposes
     */
    std::string getDecodedName() const;

    // Helper methods
    bool hasAttribute(const std::string& attr) const
    {
      return std::find(attributes.begin(), attributes.end(), attr) != attributes.end();
    }

    bool isSelectable() const
    {
      return !hasAttribute("\\Noselect");
    }

    bool hasChildren() const
    {
      return hasAttribute("\\HasChildren");
    }

    bool hasNoChildren() const
    {
      return hasAttribute("\\HasNoChildren");
    }
  };

  /**
   * @brief Result from SELECT/EXAMINE command with mailbox metadata
   *
   * Contains information needed for efficient caching and sync.
   */
  struct SelectResponse
  {
    uint32_t exists = 0;                       ///< Total messages in mailbox
    uint32_t recent = 0;                       ///< Messages with \Recent flag
    uint32_t uidvalidity = 0;                  ///< UID validity value (changes = cache invalid)
    uint32_t uidnext = 0;                      ///< Next predicted UID
    uint64_t highestmodseq = 0;                ///< Highest modification sequence (CONDSTORE)
    std::vector<std::string> flags;            ///< Available flags in mailbox
    std::vector<std::string> permanent_flags;  ///< Flags that can be permanently set
    bool read_write = true;                    ///< false if opened read-only (EXAMINE)
  };

  /**
   * @brief Result from QRESYNC SELECT with sync information
   */
  struct QresyncResponse : SelectResponse
  {
    std::vector<uint32_t> expunged_uids;  ///< UIDs that were expunged since last sync
    // Changed messages are delivered as FETCH responses via unsolicited callback
  };

  /**
   * @brief Server capabilities (extensions supported)
   */
  struct Capabilities
  {
    std::set<std::string> capabilities;

    bool has(std::string_view cap) const
    {
      return capabilities.contains(std::string(cap));
    }

    // Convenience checks for common extensions
    bool hasStartTls() const
    {
      return has("STARTTLS");
    }
    bool hasIdle() const
    {
      return has("IDLE");
    }
    bool hasCondstore() const
    {
      return has("CONDSTORE");
    }
    bool hasQresync() const
    {
      return has("QRESYNC");
    }
    bool hasUidplus() const
    {
      return has("UIDPLUS");
    }
    bool hasMove() const
    {
      return has("MOVE");
    }
    bool hasLiteralPlus() const
    {
      return has("LITERAL+") || has("LITERAL-");
    }
  };

  using namespace aurora::mail::common;
  using boost::asio::awaitable;
  using aurora::mail::common::ConnectionMode;

  /**
   * @brief Callback type for handling unsolicited server responses.
   *
   * Called when server sends untagged responses outside of command context:
   * - * 5 EXISTS (new mail)
   * - * 1 EXPUNGE (message deleted)
   * - * OK [ALERT] System message
   * - * 1 FETCH (...) (during IDLE)
   */
  using UnsolicitedCallback = std::function<void(const response::UntaggedResponse&)>;

  // Import StatusType into namespace for cleaner code
  using StatusType = response::StatusType;

  /**
   * @brief An asynchronous IMAP client supporting TLS and coroutine-based
   * communication.
   *
   * This client uses Boost.Asio awaitable coroutines to interact with IMAP
   * servers, allowing asynchronous control flow without complex callback chains.
   * It supports standard IMAP commands, authentication, TLS encryption, and
   * graceful termination of the session.
   */
  class ImapClient : public BaseProtocolClient
  {
   public:
    /**
     * @brief Constructs the IMAP client with references to the I/O and SSL
     * contexts.
     */
    ImapClient(asio::io_context& io_context, asio::ssl::context& ssl_context, int timeout_seconds)
        : BaseProtocolClient(io_context, ssl_context, timeout_seconds, MailProtocol::IMAP)
    {
    }

    /**
     * @brief Connects to an IMAP server asynchronously.
     *
     * Connection mode is automatically determined from port:
     * - Port 993: IMAPS (direct TLS)
     * - Port 143: STARTTLS (upgrade to TLS, secure default)
     * - Other: STARTTLS (safe default)
     *
     * @param hostname Server hostname or IP
     * @param port Server port
     * @return Awaitable VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncConnect(const std::string& hostname, uint16_t port);

    /**
     * @brief Authenticates the user with LOGIN command.
     *
     * @param username The username.
     * @param password The password.
     * @return Awaitable VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncLogin(const std::string& username, const std::string& password);

    /**
     * @brief Authenticates using any supported authentication method.
     *
     * Accepts any authentication command type (Login, AuthPlain,
     * AuthXOAuth2). The authentication method is automatically
     * dispatched based on the command type.
     *
     * @tparam AuthCmd Authentication command type (must satisfy ProtocolCommand)
     * @param auth Authentication command
     * @return VoidResult indicating success or error
     *
     * @example
     *   // OAuth2 authentication (caller fetches the token first)
     *   co_await client.asyncAuthenticate(
     *       imap::command::AuthXOAuth2{
     *           tag, "user@gmail.com", access_token});
     *
     *   // PLAIN authentication
     *   co_await client.asyncAuthenticate(
     *       imap::command::AuthPlain{tag, "user", "password"});
     */
    template<ProtocolCommand AuthCmd>
    awaitable<VoidResult> asyncAuthenticate(const AuthCmd& auth)
    {
      co_return co_await sendCommandAndValidate(auth, StatusType::OK);
    }

    /**
     * @brief XOAUTH2 via SASL continuation (RFC 3501): many servers (including Gmail)
     * require "TAG AUTHENTICATE XOAUTH2", then "+ ..." from server, then the base64
     */
    awaitable<VoidResult> asyncAuthenticate(const command::AuthXOAuth2& auth);

    /**
     * @brief Selects a mailbox for reading/writing.
     *
     * @param mailbox The mailbox name (e.g., "INBOX").
     * @return Awaitable VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncSelectMailbox(const std::string& mailbox);

    /**
     * @brief Examines a mailbox (read-only mode).
     *
     * @param mailbox The mailbox name.
     * @return Awaitable VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncExamineMailbox(const std::string& mailbox);

    /**
     * @brief Fetches mail messages.
     *
     * @param message_set Message sequence numbers or ranges:
     *                    - Single: "1"
     *                    - Range: "1:10", "1:*" (all from 1 onwards)
     *                    - Set: "1,3,5,7"
     * @param data_items What to fetch (e.g., "BODY[]", "RFC822", "FLAGS").
     * @return Awaitable that completes with the fetched data.
     */
    awaitable<Result<std::string>> asyncFetchMail(const std::string& message_set, const std::string& data_items = "BODY[]");

    /**
     * @brief Searches for messages matching criteria.
     *
     * @param criteria Search criteria (e.g., "UNSEEN", "FROM user@example.com").
     * @return Awaitable that completes with search results.
     */
    awaitable<Result<std::string>> asyncSearchMail(const std::string& criteria);

    /**
     * @brief Lists mailboxes matching a pattern.
     *
     * @param reference Reference name (usually "" for base)
     * @param mailbox_pattern Pattern to match (e.g., "*" for all, "INBOX.*" for
     * INBOX subfolders)
     * @return Vector of MailboxInfo objects with attributes, delimiter, and name
     */
    awaitable<Result<std::vector<MailboxInfo>>> asyncListMailboxes(
        const std::string& reference = "",
        const std::string& mailbox_pattern = "*");

    /**
     * @brief Sends NOOP command (keep-alive).
     *
     * @return Awaitable VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncNoop();

    /**
     * @brief Fetches mail messages by UID (persistent identifiers).
     *
     * @param uid_set UID or UID range: "123", "100:200", "100:*", "100,150,200"
     * @param data_items What to fetch (e.g., "BODY[]", "RFC822", "FLAGS").
     * @return Awaitable that completes with the fetched data.
     */
    awaitable<Result<std::string>> asyncUidFetchMail(const std::string& uid_set, const std::string& data_items = "BODY[]");

    /**
     * @brief Searches for messages by UID.
     *
     * @param criteria Search criteria (e.g., "UNSEEN", "FROM user@example.com").
     * @return Awaitable that completes with search results (UIDs).
     */
    awaitable<Result<std::string>> asyncUidSearchMail(const std::string& criteria);

    /**
     * @brief Modifies message flags by UID.
     *
     * @param uid_set UID or UID range: "123", "100:200", "100:*", "100,150,200"
     * @param flags_action Flag action string:
     *   - "+FLAGS (\Seen)"        - add flags
     *   - "-FLAGS (\Deleted)"     - remove flags
     *   - "FLAGS (\Seen \Flagged)" - replace flags
     *   - "+FLAGS.SILENT (\Seen)" - add without echoing (saves bandwidth)
     * @return Awaitable that completes with the server response.
     */
    awaitable<Result<std::string>> asyncUidStoreMail(const std::string& uid_set, const std::string& flags_action);

    /**
     * @brief Copies messages to another mailbox by UID.
     *
     * @param uid_set UID or UID range to copy
     * @param destination_mailbox Target mailbox name
     * @return VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncUidCopyMail(const std::string& uid_set, const std::string& destination_mailbox);

    /**
     * @brief Permanently removes messages marked \Deleted by UID.
     *
     * Unlike EXPUNGE which removes ALL messages with \Deleted flag,
     * UID EXPUNGE only removes messages in the specified UID set.
     * Requires UIDPLUS extension (RFC 4315).
     *
     * @param uid_set UIDs to expunge
     * @return VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncUidExpunge(const std::string& uid_set);

    /**
     * @brief Gets mailbox status without selecting it.
     *
     * Useful for checking mailbox state during sync without changing selection.
     *
     * @param mailbox Mailbox name
     * @param status_items Items to query, e.g.:
     *   - "MESSAGES"     - total message count
     *   - "RECENT"       - recent message count
     *   - "UIDNEXT"      - predicted next UID
     *   - "UIDVALIDITY"  - UID validity value (changes = UIDs invalidated)
     *   - "UNSEEN"       - first unseen message number
     * @return Raw status response for parsing.
     *
     * Example: asyncStatus("INBOX", "MESSAGES UIDNEXT UIDVALIDITY")
     */
    awaitable<Result<std::string>> asyncStatus(const std::string& mailbox, const std::string& status_items);

    /**
     * @brief Sends the LOGOUT command and closes the connection.
     *
     * @return Awaitable VoidResult indicating success or error.
     */
    awaitable<VoidResult> asyncLogout();

    // ==========================================================================
    // CAPABILITY - Feature Detection
    // ==========================================================================

    /**
     * @brief Queries server capabilities (extensions supported).
     *
     * Should be called after login to determine available features.
     * Common capabilities: IDLE, CONDSTORE, QRESYNC, UIDPLUS, MOVE, LITERAL+
     *
     * @return Capabilities struct with easy lookup methods
     */
    awaitable<Result<Capabilities>> asyncCapability();

    // ==========================================================================
    // IDLE - Push Notifications (RFC 2177)
    // ==========================================================================

    /**
     * @brief Enters IDLE mode for real-time push notifications.
     *
     * While in IDLE, the server pushes changes immediately:
     * - * n EXISTS (new mail arrived)
     * - * n EXPUNGE (message deleted)
     * - * n FETCH (FLAGS (...)) (flags changed)
     *
     * Call asyncIdleDone() to exit IDLE mode. IDLE times out after ~30 minutes
     * on most servers, so re-enter periodically.
     *
     * @return VoidResult after server confirms "+ idling"
     */
    awaitable<VoidResult> asyncIdleStart();

    /**
     * @brief Exits IDLE mode.
     *
     * Sends "DONE" continuation to end IDLE. Must be called before sending
     * any other command.
     *
     * @return VoidResult after server confirms IDLE ended
     */
    awaitable<VoidResult> asyncIdleDone();

    /**
     * @brief Waits for server notification while in IDLE mode.
     *
     * Blocks until server sends a notification (EXISTS, EXPUNGE, etc.),
     * timeout is reached, or cancelIdleWait() is called.
     * Use with asyncIdleStart()/asyncIdleDone().
     *
     * @param timeout_seconds Maximum time to wait (0 = indefinite)
     * @return The notification received, timeout error, or cancelled error
     */
    awaitable<Result<std::string>> asyncIdleWait(int timeout_seconds = 0);

    /**
     * @brief Cancels an ongoing asyncIdleWait() call.
     *
     * May be called from any thread; the cancel is posted to this client's
     * io_context so timer cancellation does not run synchronously from the
     * caller (avoids reactor reentrancy / cross-thread timer state issues).
     * After cancellation, asyncIdleWait() returns a cancellation error.
     */
    void cancelIdleWait();

    // ==========================================================================
    // CONDSTORE - Efficient Sync (RFC 7162)
    // ==========================================================================

    /**
     * @brief Selects mailbox with CONDSTORE for efficient sync.
     *
     * Returns HIGHESTMODSEQ which can be used to detect changes.
     * Use asyncUidFetchChangedSince() to get only modified messages.
     *
     * @param mailbox Mailbox name
     * @return SelectResponse with highestmodseq populated
     */
    awaitable<Result<SelectResponse>> asyncSelectCondstore(const std::string& mailbox);

    /**
     * @brief Examines mailbox (read-only) with CONDSTORE.
     *
     * @param mailbox Mailbox name
     * @return SelectResponse with highestmodseq populated
     */
    awaitable<Result<SelectResponse>> asyncExamineCondstore(const std::string& mailbox);

    /**
     * @brief Fetches only messages changed since a modification sequence.
     *
     * Dramatically reduces sync traffic - only returns messages whose flags
     * or other metadata changed since the given modseq.
     *
     * @param uid_set UID range (e.g., "1:*" for all)
     * @param modseq Modification sequence from previous sync
     * @param data_items What to fetch (usually "FLAGS" for sync)
     * @return Raw response with changed messages only
     */
    awaitable<Result<std::string>>
    asyncUidFetchChangedSince(const std::string& uid_set, uint64_t modseq, const std::string& data_items = "FLAGS");

    /**
     * @brief Conditionally updates flags only if message hasn't changed.
     *
     * Prevents race conditions when multiple clients modify the same message.
     * If message was modified since modseq, operation fails and returns
     * MODIFIED response code.
     *
     * @param uid_set UIDs to update
     * @param modseq Modification sequence (from previous FETCH)
     * @param flags_action Flag modification (e.g., "+FLAGS (\\Seen)")
     * @return Response (check for [MODIFIED] on conflict)
     */
    awaitable<Result<std::string>>
    asyncUidStoreUnchangedSince(const std::string& uid_set, uint64_t modseq, const std::string& flags_action);

    // ==========================================================================
    // QRESYNC - Quick Resync (RFC 7162)
    // ==========================================================================

    /**
     * @brief Enables QRESYNC extension (required before using QRESYNC SELECT).
     *
     * Must be called once after login if server supports QRESYNC.
     * Also implicitly enables CONDSTORE.
     *
     * @return VoidResult
     */
    awaitable<VoidResult> asyncEnableQresync();

    /**
     * @brief Selects mailbox with QRESYNC for instant sync.
     *
     * Server immediately tells you:
     * - What messages were expunged since last sync
     * - What messages' flags changed (via FETCH responses)
     *
     * This is the most efficient sync method available.
     *
     * @param mailbox Mailbox name
     * @param uidvalidity Expected UIDVALIDITY (from cache)
     * @param modseq Highest modseq from last sync
     * @param known_uids Optional: comma-separated UID ranges you have cached
     * @return QresyncResponse with expunged UIDs (changes come via callback)
     */
    awaitable<Result<QresyncResponse>> asyncSelectQresync(
        const std::string& mailbox,
        uint32_t uidvalidity,
        uint64_t modseq,
        const std::string& known_uids = "");

    // ==========================================================================
    // Mailbox Management
    // ==========================================================================

    /**
     * @brief Creates a new mailbox.
     *
     * @param mailbox Mailbox name (use hierarchy delimiter for nested)
     * @return VoidResult
     */
    awaitable<VoidResult> asyncCreateMailbox(const std::string& mailbox);

    /**
     * @brief Deletes a mailbox.
     *
     * Mailbox must be empty (no messages) on most servers.
     *
     * @param mailbox Mailbox name
     * @return VoidResult
     */
    awaitable<VoidResult> asyncDeleteMailbox(const std::string& mailbox);

    /**
     * @brief Renames a mailbox.
     *
     * @param old_name Current mailbox name
     * @param new_name New mailbox name
     * @return VoidResult
     */
    awaitable<VoidResult> asyncRenameMailbox(const std::string& old_name, const std::string& new_name);

    /**
     * @brief Subscribes to a mailbox (marks as "active" for LSUB).
     *
     * @param mailbox Mailbox name
     * @return VoidResult
     */
    awaitable<VoidResult> asyncSubscribe(const std::string& mailbox);

    /**
     * @brief Unsubscribes from a mailbox.
     *
     * @param mailbox Mailbox name
     * @return VoidResult
     */
    awaitable<VoidResult> asyncUnsubscribe(const std::string& mailbox);

    /**
     * @brief Lists subscribed mailboxes.
     *
     * @param reference Reference name (usually "")
     * @param pattern Pattern (usually "*")
     * @return Vector of subscribed MailboxInfo
     */
    awaitable<Result<std::vector<MailboxInfo>>> asyncListSubscribed(
        const std::string& reference = "",
        const std::string& pattern = "*");

    // ==========================================================================
    // Message Upload
    // ==========================================================================

    /**
     * @brief Uploads a message to a mailbox (APPEND).
     *
     * @param mailbox Destination mailbox
     * @param message Raw RFC 5322 message (with CRLF line endings)
     * @param flags Optional flags to set (e.g., "(\\Seen \\Draft)")
     * @param date_time Optional internal date (DD-Mon-YYYY HH:MM:SS +ZZZZ)
     * @return VoidResult
     */
    awaitable<VoidResult> asyncAppend(
        const std::string& mailbox,
        const std::string& message,
        const std::string& flags = "",
        const std::string& date_time = "");

    // ==========================================================================
    // UID MOVE (RFC 6851)
    // ==========================================================================

    /**
     * @brief Moves messages to another mailbox by UID.
     *
     * More efficient than COPY + STORE \Deleted + EXPUNGE.
     * Requires MOVE capability.
     *
     * @param uid_set UIDs to move
     * @param destination_mailbox Target mailbox
     * @return VoidResult
     */
    awaitable<VoidResult> asyncUidMove(const std::string& uid_set, const std::string& destination_mailbox);

    /**
     * @brief Closes the current mailbox, expunging deleted messages.
     *
     * Unlike EXPUNGE, CLOSE doesn't return EXPUNGE responses.
     * This is faster when you don't need to know which messages were deleted.
     *
     * @return VoidResult
     */
    awaitable<VoidResult> asyncClose();

    /**
     * @brief Expunges all messages marked \Deleted in the current mailbox.
     *
     * Unlike UID EXPUNGE which removes specific UIDs, this removes ALL
     * messages with the \Deleted flag.
     *
     * @return VoidResult
     */
    awaitable<VoidResult> asyncExpunge();


    /**
     * @brief Registers a callback for unsolicited server responses.
     *
     * IMAP servers send untagged responses at any time:
     * - * 5 EXISTS (new mail notification)
     * - * 1 EXPUNGE (message deleted)
     * - * OK [ALERT] Server message
     * - * 1 FETCH (...) (during IDLE command)
     *
     * @param callback Function called for each unsolicited response
     */
    void setUnsolicitedCallback(UnsolicitedCallback callback)
    {
      unsolicited_callback_ = std::move(callback);
    }

    /**
     * @brief Clears the unsolicited response callback.
     */
    void clearUnsolicitedCallback()
    {
      unsolicited_callback_ = std::nullopt;
    }

    /**
     * @brief Resets the client to initial state without closing the socket.
     */
    void reset();

   protected:
    /**
     * @brief Detects IMAP literal syntax {size} at end of line.
     *
     * IMAP uses literals for binary data: "BODY[] {1234}" means next 1234 bytes
     * are literal data that may contain CRLF.
     *
     * @param line Current response line
     * @return Literal size if {size} pattern found, std::nullopt otherwise
     */
    std::optional<std::size_t> detectLiteralSize(std::string_view line) const override;

   private:
    std::string m_current_mailbox;
    ::aurora::mail::common::TagGenerator m_tag_generator;
    std::optional<UnsolicitedCallback> unsolicited_callback_;

    // IDLE state
    std::string idle_tag_;  ///< Tag used for current IDLE command
    bool in_idle_ = false;  ///< Whether we're currently in IDLE mode
    /// Set only while asyncIdleWait's parallel_group is active; cancelIdleWait emits on this signal.
    /// Per-wait shared_ptr avoids stale emit after a wait ends; see asyncIdleWait implementation.
    std::shared_ptr<boost::asio::cancellation_signal> idle_cancel_outstanding_;

    // Cached capabilities
    std::optional<Capabilities> capabilities_;

    /// True when `line` is the tagged completion for the command in `serialized` (TAG ...\\r\\n).
    /// Ignores other commands' tagged lines so overlapping reads cannot close the wrong operation.
    static bool isResponseFinalForSerializedCommand(std::string_view serialized_command, const std::string& line);

    // IMAP-specific predicate: check if this is a tagged response (final line)
    // Tagged responses have format: <tag> <status> [text]
    // e.g., "A001 OK Success", "A002 NO [AUTHENTICATIONFAILED] Invalid"
    // Untagged responses start with '*' (e.g., "* CAPABILITY IMAP4rev1")
    static bool isImapFinalLine(const std::string& line)
    {
      if (line.empty() || line[0] == '*' || line[0] == '+')
      {
        return false;
      }
      // Check if line matches tagged response pattern: <tag> <space> <OK|NO|BAD>
      // Tag is typically A followed by digits (e.g., A001, A123)
      size_t space_pos = line.find(' ');
      if (space_pos == std::string::npos || space_pos == 0)
      {
        return false;
      }
      // Check if the part after the space starts with OK, NO, or BAD
      std::string_view after_tag(line.data() + space_pos + 1, line.size() - space_pos - 1);
      return after_tag.starts_with("OK") || after_tag.starts_with("NO") || after_tag.starts_with("BAD");
    }

    /**
     * @brief Determine IMAP connection mode from port number.
     *
     * Standard ports:
     * - 993: IMAPS (direct TLS)
     * - 143: STARTTLS (upgrade to TLS, secure default - no PLAIN support)
     * - Other: STARTTLS (safe default)
     *
     * Note: We don't support PLAIN mode for IMAP to avoid security issues.
     * Port 143 always uses STARTTLS for encryption.
     */
    ConnectionMode portToConnectionMode(uint16_t port);

    /**
     * @brief Reads the server greeting.
     *
     * @return Result containing the greeting string or error.
     */
    awaitable<Result<std::string>> readGreeting();

    /**
     * @brief Sends a command and reads the response.
     *
     * @param command The IMAP command to send.
     * @return Result containing the response string or error.
     */
    awaitable<Result<std::string>> sendCommandAndReadResponse(const ::aurora::mail::imap::command::Command& command);

    /**
     * @brief Parses and validates an IMAP response.
     *
     * Also processes untagged responses and calls unsolicited callback if set.
     *
     * @param raw_response The raw response string.
     * @param expected_status The expected status type.
     * @return Result containing parsed response or error.
     */
    Result<response::ImapResponse> parseAndValidate(const std::string& raw_response, StatusType expected_status);

    /**
     * @brief Processes untagged responses and invokes callback for unsolicited
     * ones.
     *
     * @param response Parsed IMAP response with untagged data
     */
    void processUntaggedResponses(const response::ImapResponse& response);

    /**
     * @brief Send command, read response, parse and validate in one call.
     *
     * Template method that accepts any IMAP command struct with serialize()
     * and name() methods. Automatically extracts command name for logging.
     *
     * @param command IMAP command (Login, Select, Logout, etc.)
     * @param expected_status Expected IMAP status (OK, NO, BAD)
     * @return VoidResult indicating success or error with details
     */
    template<ProtocolCommand Cmd>
    awaitable<VoidResult> sendCommandAndValidate(const Cmd& command, StatusType expected_status)
    {
      // Constraints checked at compile-time via ProtocolCommand concept
      auto serialized_result = command.serialize();
      if (!serialized_result.has_value())
      {
        co_return std::unexpected(serialized_result.error());
      }
      std::string serialized = std::move(serialized_result).value();
      std::string_view cmd_name = Cmd::name();

      // Send command and read response (match this command's tag only)
      auto response = co_await BaseProtocolClient::sendCommandAndReadResponse(
          serialized,
          [serialized](const std::string& line) { return isResponseFinalForSerializedCommand(serialized, line); });

      if (!response.has_value())
      {
        co_return std::unexpected(response.error());
      }

      // Parse and validate
      auto parsed = parseAndValidate(response.value(), expected_status);
      if (!parsed.has_value())
      {
        co_return std::unexpected(parsed.error());
      }

      log_debug(
          std::format("IMAP: {} succeeded (status: {})", cmd_name, response::statusTypeToString(parsed.value().status)));
      co_return VoidResult{};
    }
  };

}  // namespace aurora::mail::imap

#endif  // IMAP_CLIENT_HPP
