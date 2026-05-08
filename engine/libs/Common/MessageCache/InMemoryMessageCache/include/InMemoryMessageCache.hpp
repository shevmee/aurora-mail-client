#ifndef IN_MEMORY_MESSAGE_CACHE_HPP
#define IN_MEMORY_MESSAGE_CACHE_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "CachedMessage.hpp"
#include "IMessageCache.hpp"

namespace aurora::mail::common::message_cache
{

  /**
   * @brief Simple in-memory message cache using std::map.
   *
   * This is a concrete implementation of the IMessageCache interface.
   * Fully supports CONDSTORE and QRESYNC for efficient synchronization.
   *
   * In production, this could be swapped out for a database-backed cache.
   */
  class InMemoryMessageCache : public IMessageCache
  {
   public:
    void setMailboxState(const std::string& mailbox, uint32_t uidvalidity, uint64_t highestmodseq) override;

    void updateHighestModSeq(const std::string& mailbox, uint64_t highestmodseq) override;

    bool isValidForQresync(const std::string& mailbox) const override;

    // ==========================================================================
    // Message Operations
    // ==========================================================================

    void addMessage(const std::string& mailbox, const CachedMessage& msg) override;

    void addMessages(const std::string& mailbox, const std::vector<CachedMessage>& messages) override;

    void removeMessage(const std::string& mailbox, uint32_t uid) override;

    void removeMessages(const std::string& mailbox, const std::vector<uint32_t>& uids) override;

    void updateFlags(const std::string& mailbox, uint32_t uid, const std::string& flags, uint64_t modseq = 0) override;

    std::optional<CachedMessage> getMessage(const std::string& mailbox, uint32_t uid) const override;

    std::vector<CachedMessage> getMessagesChangedSince(const std::string& mailbox, uint64_t since_modseq) const override;

    // ==========================================================================
    // Body Content Operations
    // ==========================================================================

    void setMessageBody(const std::string& mailbox, uint32_t uid, const std::string& text_body, const std::string& html_body)
        override;

    void setMessageHeaders(const std::string& mailbox, uint32_t uid, const std::string& raw_headers) override;

    void setMessageAttachments(const std::string& mailbox, uint32_t uid, const std::vector<CachedAttachment>& attachments)
        override;

    bool hasBody(const std::string& mailbox, uint32_t uid) const override;

    // ==========================================================================
    // Mailbox Metadata
    // ==========================================================================

    std::optional<uint32_t> getUidValidity(const std::string& mailbox) const override;
    std::optional<uint64_t> getHighestModSeq(const std::string& mailbox) const override;

    size_t messageCount(const std::string& mailbox) const override;

    std::vector<uint32_t> getCachedUids(const std::string& mailbox) const override;

    std::string getKnownUidRanges(const std::string& mailbox) const override;

    std::optional<uint32_t> getUidNext(const std::string& mailbox) const override;
    void setUidNext(const std::string& mailbox, uint32_t uidnext) override;

    void clear(const std::string& mailbox) override;
    void clearAll() override;

   private:
    struct MailboxState
    {
      uint32_t uidvalidity = 0;
      uint64_t highestmodseq = 0;
      uint32_t uidnext = 0;
      std::map<uint32_t, CachedMessage> messages;  // UID -> message
    };

    const MailboxState* getMailboxState(const std::string& mailbox) const;
    MailboxState* getMailboxState(const std::string& mailbox);
    MailboxState& getOrCreateMailboxState(const std::string& mailbox);

    std::map<std::string, MailboxState> mailboxes_;
  };

}  // namespace aurora::mail::common::message_cache

#endif  // IN_MEMORY_MESSAGE_CACHE_HPP