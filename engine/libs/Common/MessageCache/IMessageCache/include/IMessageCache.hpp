#ifndef I_MESSAGE_CACHE_HPP
#define I_MESSAGE_CACHE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CachedMessage.hpp"

namespace aurora::mail::common::message_cache
{

  /**
   * @brief Interface for a message cache.
   *
   * Defines the contract for different caching strategies (e.g., in-memory,
   * Postgres). This allows the application to be independent of the specific
   * cache implementation.
   *
   * Supports IMAP CONDSTORE (RFC 7162) for efficient synchronization:
   * - Tracks UIDVALIDITY to detect cache invalidation
   * - Tracks HIGHESTMODSEQ for incremental sync
   * - Per-message MODSEQ for change detection
   *
   * Supports QRESYNC (RFC 7162) for instant resync:
   * - Batch removal of expunged messages
   * - Known UID ranges for SELECT QRESYNC
   */
  class IMessageCache
  {
   public:
    virtual ~IMessageCache() = default;

    // ==========================================================================
    // Mailbox State Management
    // ==========================================================================

    /**
     * @brief Set or update mailbox state from SELECT response.
     *
     * If UIDVALIDITY changes, the cache for this mailbox is invalidated.
     */
    virtual void setMailboxState(const std::string& mailbox, uint32_t uidvalidity, uint64_t highestmodseq) = 0;

    /**
     * @brief Update only the HIGHESTMODSEQ after a sync operation.
     */
    virtual void updateHighestModSeq(const std::string& mailbox, uint64_t highestmodseq) = 0;

    /**
     * @brief Check if cache is valid for QRESYNC.
     *
     * @return true if mailbox has valid UIDVALIDITY and HIGHESTMODSEQ
     */
    virtual bool isValidForQresync(const std::string& mailbox) const = 0;

    // ==========================================================================
    // Message Operations
    // ==========================================================================

    /**
     * @brief Add or update a message in the cache.
     */
    virtual void addMessage(const std::string& mailbox, const CachedMessage& msg) = 0;

    /**
     * @brief Add or update multiple messages (batch operation).
     */
    virtual void addMessages(const std::string& mailbox, const std::vector<CachedMessage>& messages) = 0;

    /**
     * @brief Remove a single message by UID.
     */
    virtual void removeMessage(const std::string& mailbox, uint32_t uid) = 0;

    /**
     * @brief Remove multiple messages by UID (for QRESYNC VANISHED responses).
     */
    virtual void removeMessages(const std::string& mailbox, const std::vector<uint32_t>& uids) = 0;

    /**
     * @brief Update flags for a message.
     *
     * @param mailbox Mailbox name the message belongs to.
     * @param uid     Message UID inside @p mailbox.
     * @param flags   Space-separated IMAP flag list (e.g. "\\Seen \\Answered").
     * @param modseq  Optional new MODSEQ for the message.
     */
    virtual void updateFlags(const std::string& mailbox, uint32_t uid, const std::string& flags, uint64_t modseq = 0) = 0;

    /**
     * @brief Get a cached message by UID.
     */
    virtual std::optional<CachedMessage> getMessage(const std::string& mailbox, uint32_t uid) const = 0;

    /**
     * @brief Get messages changed since a given MODSEQ (for local queries).
     */
    virtual std::vector<CachedMessage> getMessagesChangedSince(const std::string& mailbox, uint64_t since_modseq) const = 0;

    // ==========================================================================
    // Body Content Operations
    // ==========================================================================

    /**
     * @brief Store body content for a message.
     *
     * @param mailbox   Mailbox name the message belongs to.
     * @param uid       Message UID inside @p mailbox.
     * @param text_body Plain-text body content (may be empty).
     * @param html_body HTML body content (may be empty).
     */
    virtual void
    setMessageBody(const std::string& mailbox, uint32_t uid, const std::string& text_body, const std::string& html_body) = 0;

    /**
     * @brief Store raw headers for a message.
     */
    virtual void setMessageHeaders(const std::string& mailbox, uint32_t uid, const std::string& raw_headers) = 0;

    /**
     * @brief Store attachment metadata for a message.
     */
    virtual void
    setMessageAttachments(const std::string& mailbox, uint32_t uid, const std::vector<CachedAttachment>& attachments) = 0;

    /**
     * @brief Check if body is cached for a message.
     */
    virtual bool hasBody(const std::string& mailbox, uint32_t uid) const = 0;

    // ==========================================================================
    // Mailbox Metadata
    // ==========================================================================

    virtual std::optional<uint32_t> getUidValidity(const std::string& mailbox) const = 0;
    virtual std::optional<uint64_t> getHighestModSeq(const std::string& mailbox) const = 0;

    virtual size_t messageCount(const std::string& mailbox) const = 0;

    /**
     * @brief Get all cached UIDs for a mailbox.
     */
    virtual std::vector<uint32_t> getCachedUids(const std::string& mailbox) const = 0;

    /**
     * @brief Get UID ranges as string for QRESYNC SELECT.
     *
     * @return Comma-separated UID ranges (e.g., "1:100,150:200,300")
     */
    virtual std::string getKnownUidRanges(const std::string& mailbox) const = 0;

    /**
     * @brief Get UIDNEXT (next expected UID) if tracked.
     */
    virtual std::optional<uint32_t> getUidNext(const std::string& mailbox) const = 0;

    /**
     * @brief Update UIDNEXT value.
     */
    virtual void setUidNext(const std::string& mailbox, uint32_t uidnext) = 0;

    // ==========================================================================
    // Cache Management
    // ==========================================================================

    virtual void clear(const std::string& mailbox) = 0;
    virtual void clearAll() = 0;
  };

}  // namespace aurora::mail::common::message_cache

#endif  // I_MESSAGE_CACHE_HPP