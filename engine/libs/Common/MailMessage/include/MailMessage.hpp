#ifndef MAIL_MSG_HPP
#define MAIL_MSG_HPP

#include <MailAddress.hpp>
#include <MailAttachment.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "EmailRecipients.hpp"
#include "EmailThreading.hpp"

namespace aurora::mail::common::mail
{

  /**
   * @brief Represents an email message.
   *
   * The `MailMessage` struct contains all components of an email message,
   * including sender, recipients (To, CC, BCC), subject, body text, and
   * attachments.
   */
  struct MailMessage
  {
    /**
     * @brief The date of the message.
     */
    std::chrono::system_clock::time_point date;

    /**
     * @brief The sender's email address.
     */
    MailAddress from;

    EmailRecipients email_recipients;

    /**
     * @brief The reply-to email address.
     */
    std::optional<MailAddress> reply_to;

    /**
     * @brief Subject line of the email.
     */
    std::string subject;

    EmailThreading email_threading;

    /**
     * @brief Body content of the email (plain text).
     */
    std::string text_body;

    /**
     * @brief List of file attachments to include with the email.
     */
    std::vector<MailAttachment> attachments_;

    /**
     * @brief The domain of the sender's email address.
     */
    std::string sender_domain;

    /// Check if this is a reply (has threading context)
    bool isReply() const
    {
      return email_threading.in_reply_to.has_value();
    }
  };

}  // namespace aurora::mail::common::mail

#endif  // MAIL_MSG_HPP
