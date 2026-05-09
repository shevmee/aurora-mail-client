#ifndef EMAIL_THREADING_HPP
#define EMAIL_THREADING_HPP
#include <optional>
#include <string>
#include <vector>

namespace aurora::mail::common::mail
{
  /**
   * @brief RFC 2822 / 5322 threading metadata for an outgoing or received
   *        message.
   *
   * Carries the three header fields that uniquely identify a message and tie
   * it into a conversation:
   *   - `message_id` &mdash; canonical `Message-ID` of *this* message.
   *   - `in_reply_to` &mdash; immediate parent's Message-ID, if any.
   *   - `references`  &mdash; full ancestor chain (oldest first), per RFC 5322 §3.6.4.
   *
   * Used by @ref aurora::mail::common::mime::writer when composing replies and
   * by @ref aurora::mail::common::mime::reader when ingesting received mail.
   */
  struct EmailThreading
  {
    /// Canonical `Message-ID` header value (without angle brackets).
    std::string message_id;
    /// Parent message's `Message-ID`, present only on replies.
    std::optional<std::string> in_reply_to;
    /// Ancestor `Message-ID`s in chronological order (oldest first).
    std::vector<std::string> references;

    /// @returns `true` iff this message threads onto an existing conversation.
    bool isReply() const
    {
      return in_reply_to.has_value();
    }
  };
}  // namespace aurora::mail::common::mail

#endif  // EMAIL_THREADING_HPP