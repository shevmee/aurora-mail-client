#ifndef IMAP_CLI_HPP
#define IMAP_CLI_HPP

#include <BaseCli.hpp>
#include <ImapClient.hpp>
#include <InMemoryMessageCache.hpp>
#include <memory>
#include <optional>

namespace aurora::mail::cli
{

  // Import cache types for convenience
  using aurora::mail::common::message_cache::CachedMessage;
  using aurora::mail::common::message_cache::InMemoryMessageCache;

  class ImapCli : public BaseCli
  {
   private:
    std::unique_ptr<aurora::mail::imap::ImapClient> imap_client;

    // IMAP-specific state
    std::string current_mailbox;
    uint32_t current_uidvalidity = 0;    // From last SELECT response
    uint64_t current_highestmodseq = 0;  // From last SELECT response
    InMemoryMessageCache cache;
    bool in_idle = false;
    bool qresync_enabled = false;  // Track if QRESYNC was enabled for this session

   public:
    ImapCli(asio::io_context& io_ctx, ssl::context& ssl_ctx, const aurora::mail::common::config::StartupConfig& cfg);

   protected:
    std::string getPrompt() const override;
    std::string getWelcomeMessage() const override;
    void initializeCommands() override;

    void handleUnsolicitedResponse(const aurora::mail::imap::response::UntaggedResponse& resp);

    // --- Connection ---
    void handleConnect(const std::vector<std::string>& args);
    void handleLogin(const std::vector<std::string>& args);
    void handleLogout();

    // --- Mailbox Operations ---
    void handleList(const std::vector<std::string>& args);
    void handleSelect(const std::vector<std::string>& args);
    void handleExamine(const std::vector<std::string>& args);
    void handleStatus(const std::vector<std::string>& args);
    void handleCreate(const std::vector<std::string>& args);
    void handleDelete(const std::vector<std::string>& args);
    void handleRename(const std::vector<std::string>& args);
    void handleSubscribe(const std::vector<std::string>& args);
    void handleUnsubscribe(const std::vector<std::string>& args);
    void handleLsub(const std::vector<std::string>& args);
    void handleClose();
    void handleExpunge();

    // --- UID-based Message Operations ---
    void handleUidSearch(const std::vector<std::string>& args);
    void handleUidFetch(const std::vector<std::string>& args);
    void handleUidStore(const std::vector<std::string>& args);
    void handleUidCopy(const std::vector<std::string>& args);
    void handleUidMove(const std::vector<std::string>& args);
    void handleUidExpunge(const std::vector<std::string>& args);

    // --- Message Reading ---
    void handleRead(const std::vector<std::string>& args);
    void displayCachedMessage(const CachedMessage& msg);
    void parseBodyResponse(const std::string& response, std::string& text_body, std::string& html_body);

    // --- Sync and Headers ---
    void handleSync(const std::vector<std::string>& args);
    void handleFetchHeaders(const std::vector<std::string>& args);

    // --- Message Upload ---
    void handleAppend(const std::vector<std::string>& args);

    // --- IDLE ---
    void handleIdle(const std::vector<std::string>& args);

    // --- Cache Operations ---
    void handleCacheInfo(const std::vector<std::string>& args);
    void handleClearCache();

    // --- Utility ---
    void handleCapability();
    void handleNoop();

    void showHelp(const std::vector<std::string>& args) override;

   private:
    // --- FETCH Response Parsing ---
    /**
     * @brief Parse a FETCH response line and extract message data.
     *
     * Handles responses like:
     * * 1 FETCH (UID 123 FLAGS (\Seen) RFC822.SIZE 4567 MODSEQ (12345) ENVELOPE
     * (...))
     */
    std::optional<CachedMessage> parseFetchResponse(const std::string& line);

    /**
     * @brief Parse multiple FETCH responses and update cache.
     * @return Number of messages parsed and cached.
     */
    size_t parseFetchResponsesAndUpdateCache(const std::string& response);

    /**
     * @brief Parse UID set (e.g., "1,2,5:10") into individual UIDs.
     */
    std::vector<uint32_t> parseUidSet(const std::string& uid_set);

    /**
     * @brief Parse VANISHED response for QRESYNC.
     * Format: * VANISHED 1:5,10,15:20
     */
    std::vector<uint32_t> parseVanishedResponse(const std::string& data);

    /**
     * @brief Parse IMAP ENVELOPE structure and populate message fields.
     *
     * ENVELOPE format: ("date" "subject" ((from)) ((sender)) ((reply-to))
     *                   ((to)) ((cc)) ((bcc)) "in-reply-to" "message-id")
     */
    void parseEnvelope(const std::string& envelope_str, CachedMessage& msg);

    /**
     * @brief Parse an IMAP address list like ((name NIL mailbox host)).
     * @return Formatted address string like "Name <mailbox@host>"
     */
    std::string parseAddressList(const std::string& addr_list);

    /**
     * @brief Decode RFC 2047 encoded header (e.g., =?UTF-8?B?...?=).
     */
    std::string decodeRfc2047(const std::string& encoded);

    /**
     * @brief Extract a quoted string or NIL from ENVELOPE at given position.
     * @return The extracted string and the new position after it.
     */
    std::pair<std::string, size_t> extractQuotedOrNil(const std::string& s, size_t pos);

    /**
     * @brief Find matching closing parenthesis.
     */
    size_t findMatchingParen(const std::string& s, size_t open_pos);
  };

}  // namespace aurora::mail::cli

#endif  // IMAP_CLI_HPP
