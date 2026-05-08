#include <InMemoryMessageCache.hpp>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace aurora::mail::common::message_cache
{

  void InMemoryMessageCache::setMailboxState(const std::string& mailbox, uint32_t uidvalidity, uint64_t highestmodseq)
  {
    auto& state = getOrCreateMailboxState(mailbox);

    // If UIDVALIDITY changed, invalidate the cache (RFC 7162 requirement)
    if (state.uidvalidity != 0 && state.uidvalidity != uidvalidity)
    {
      std::cout << "[Cache] UIDVALIDITY changed for " << mailbox << " (" << state.uidvalidity << " -> " << uidvalidity
                << "). Invalidating cache.\n";
      state.messages.clear();
      state.uidnext = 0;
    }

    state.uidvalidity = uidvalidity;
    state.highestmodseq = highestmodseq;
  }

  void InMemoryMessageCache::updateHighestModSeq(const std::string& mailbox, uint64_t highestmodseq)
  {
    auto* state = getMailboxState(mailbox);
    if (state && highestmodseq > state->highestmodseq)
    {
      state->highestmodseq = highestmodseq;
    }
  }

  bool InMemoryMessageCache::isValidForQresync(const std::string& mailbox) const
  {
    const auto* state = getMailboxState(mailbox);
    return state != nullptr && state->uidvalidity > 0 && state->highestmodseq > 0 && !state->messages.empty();
  }

  void InMemoryMessageCache::addMessage(const std::string& mailbox, const CachedMessage& msg)
  {
    auto& state = getOrCreateMailboxState(mailbox);
    state.messages[msg.uid] = msg;

    // Update highest modseq if this message has a higher one
    if (msg.modseq > state.highestmodseq)
    {
      state.highestmodseq = msg.modseq;
    }
  }

  void InMemoryMessageCache::addMessages(const std::string& mailbox, const std::vector<CachedMessage>& messages)
  {
    if (messages.empty())
      return;

    auto& state = getOrCreateMailboxState(mailbox);
    uint64_t max_modseq = state.highestmodseq;

    for (const auto& msg : messages)
    {
      state.messages[msg.uid] = msg;
      if (msg.modseq > max_modseq)
      {
        max_modseq = msg.modseq;
      }
    }

    state.highestmodseq = max_modseq;
  }

  void InMemoryMessageCache::removeMessage(const std::string& mailbox, uint32_t uid)
  {
    auto* state = getMailboxState(mailbox);
    if (state)
    {
      state->messages.erase(uid);
    }
  }

  void InMemoryMessageCache::removeMessages(const std::string& mailbox, const std::vector<uint32_t>& uids)
  {
    auto* state = getMailboxState(mailbox);
    if (!state)
      return;

    for (uint32_t uid : uids)
    {
      state->messages.erase(uid);
    }
  }

  void InMemoryMessageCache::updateFlags(const std::string& mailbox, uint32_t uid, const std::string& flags, uint64_t modseq)
  {
    auto* state = getMailboxState(mailbox);
    if (!state)
      return;

    auto msg_it = state->messages.find(uid);
    if (msg_it != state->messages.end())
    {
      msg_it->second.flags = flags;
      if (modseq > 0)
      {
        msg_it->second.modseq = modseq;
        if (modseq > state->highestmodseq)
        {
          state->highestmodseq = modseq;
        }
      }
    }
  }

  std::optional<CachedMessage> InMemoryMessageCache::getMessage(const std::string& mailbox, uint32_t uid) const
  {
    const auto* state = getMailboxState(mailbox);
    if (!state)
      return std::nullopt;

    auto msg_it = state->messages.find(uid);
    if (msg_it != state->messages.end())
    {
      return msg_it->second;
    }
    return std::nullopt;
  }

  std::vector<CachedMessage> InMemoryMessageCache::getMessagesChangedSince(const std::string& mailbox, uint64_t since_modseq)
      const
  {
    std::vector<CachedMessage> result;

    const auto* state = getMailboxState(mailbox);
    if (!state)
      return result;

    for (const auto& [uid, msg] : state->messages)
    {
      if (msg.modseq > since_modseq)
      {
        result.push_back(msg);
      }
    }

    return result;
  }

  // ==========================================================================
  // Body Content Operations
  // ==========================================================================

  void InMemoryMessageCache::setMessageBody(
      const std::string& mailbox,
      uint32_t uid,
      const std::string& text_body,
      const std::string& html_body)
  {
    auto* state = getMailboxState(mailbox);
    if (!state)
      return;

    auto msg_it = state->messages.find(uid);
    if (msg_it != state->messages.end())
    {
      msg_it->second.text_body = text_body;
      msg_it->second.html_body = html_body;
      msg_it->second.has_body = true;
      msg_it->second.generatePreview();
    }
  }

  void InMemoryMessageCache::setMessageHeaders(const std::string& mailbox, uint32_t uid, const std::string& raw_headers)
  {
    auto* state = getMailboxState(mailbox);
    if (!state)
      return;

    auto msg_it = state->messages.find(uid);
    if (msg_it != state->messages.end())
    {
      msg_it->second.raw_headers = raw_headers;
    }
  }

  void InMemoryMessageCache::setMessageAttachments(
      const std::string& mailbox,
      uint32_t uid,
      const std::vector<CachedAttachment>& attachments)
  {
    auto* state = getMailboxState(mailbox);
    if (!state)
      return;

    auto msg_it = state->messages.find(uid);
    if (msg_it != state->messages.end())
    {
      msg_it->second.attachments = attachments;
      msg_it->second.has_structure = true;
    }
  }

  bool InMemoryMessageCache::hasBody(const std::string& mailbox, uint32_t uid) const
  {
    const auto* state = getMailboxState(mailbox);
    if (!state)
      return false;

    auto msg_it = state->messages.find(uid);
    if (msg_it != state->messages.end())
    {
      return msg_it->second.has_body;
    }
    return false;
  }

  std::optional<uint32_t> InMemoryMessageCache::getUidValidity(const std::string& mailbox) const
  {
    const auto* state = getMailboxState(mailbox);
    if (state && state->uidvalidity > 0)
    {
      return state->uidvalidity;
    }
    return std::nullopt;
  }

  std::optional<uint64_t> InMemoryMessageCache::getHighestModSeq(const std::string& mailbox) const
  {
    const auto* state = getMailboxState(mailbox);
    if (state && state->highestmodseq > 0)
    {
      return state->highestmodseq;
    }
    return std::nullopt;
  }

  size_t InMemoryMessageCache::messageCount(const std::string& mailbox) const
  {
    const auto* state = getMailboxState(mailbox);
    return state ? state->messages.size() : 0;
  }

  std::vector<uint32_t> InMemoryMessageCache::getCachedUids(const std::string& mailbox) const
  {
    std::vector<uint32_t> uids;
    const auto* state = getMailboxState(mailbox);
    if (!state)
      return uids;

    uids.reserve(state->messages.size());
    for (const auto& [uid, _] : state->messages)
    {
      uids.push_back(uid);
    }
    return uids;
  }

  std::string InMemoryMessageCache::getKnownUidRanges(const std::string& mailbox) const
  {
    auto uids = getCachedUids(mailbox);
    if (uids.empty())
      return "";

    // Sort UIDs (should already be sorted from map, but ensure)
    std::sort(uids.begin(), uids.end());

    // Compress into ranges for efficient QRESYNC
    std::ostringstream oss;
    uint32_t range_start = uids[0];
    uint32_t range_end = uids[0];

    for (size_t i = 1; i < uids.size(); ++i)
    {
      if (uids[i] == range_end + 1)
      {
        // Extend current range
        range_end = uids[i];
      }
      else
      {
        // Output current range and start new one
        if (!oss.str().empty())
          oss << ",";
        if (range_start == range_end)
        {
          oss << range_start;
        }
        else
        {
          oss << range_start << ":" << range_end;
        }
        range_start = range_end = uids[i];
      }
    }

    // Output final range
    if (!oss.str().empty())
      oss << ",";
    if (range_start == range_end)
    {
      oss << range_start;
    }
    else
    {
      oss << range_start << ":" << range_end;
    }

    return oss.str();
  }

  std::optional<uint32_t> InMemoryMessageCache::getUidNext(const std::string& mailbox) const
  {
    const auto* state = getMailboxState(mailbox);
    if (state && state->uidnext > 0)
    {
      return state->uidnext;
    }
    return std::nullopt;
  }

  void InMemoryMessageCache::setUidNext(const std::string& mailbox, uint32_t uidnext)
  {
    auto& state = getOrCreateMailboxState(mailbox);
    state.uidnext = uidnext;
  }

  // ==========================================================================
  // Cache Management
  // ==========================================================================

  void InMemoryMessageCache::clear(const std::string& mailbox)
  {
    auto* state = getMailboxState(mailbox);
    if (state)
    {
      state->messages.clear();
      // Keep uidvalidity and highestmodseq for potential reconnection
    }
  }

  void InMemoryMessageCache::clearAll()
  {
    mailboxes_.clear();
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  const InMemoryMessageCache::MailboxState* InMemoryMessageCache::getMailboxState(const std::string& mailbox) const
  {
    auto it = mailboxes_.find(mailbox);
    return it != mailboxes_.end() ? &it->second : nullptr;
  }

  InMemoryMessageCache::MailboxState* InMemoryMessageCache::getMailboxState(const std::string& mailbox)
  {
    auto it = mailboxes_.find(mailbox);
    return it != mailboxes_.end() ? &it->second : nullptr;
  }

  InMemoryMessageCache::MailboxState& InMemoryMessageCache::getOrCreateMailboxState(const std::string& mailbox)
  {
    return mailboxes_[mailbox];
  }

}  // namespace aurora::mail::common::message_cache
