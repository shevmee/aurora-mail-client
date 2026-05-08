#include <MailMessageBuilder.hpp>

namespace aurora::mail::common::mail
{
  MailMessageBuilder& MailMessageBuilder::from(const MailAddress& sender)
  {
    message_.from = sender;
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::to(const MailAddress& recipient)
  {
    message_.email_recipients.to.emplace_back(recipient);
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::to(std::vector<MailAddress> recipients)
  {
    message_.email_recipients.to.insert(
        message_.email_recipients.to.end(),
        std::make_move_iterator(recipients.begin()),
        std::make_move_iterator(recipients.end()));
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::cc(const MailAddress& recipient)
  {
    message_.email_recipients.cc.emplace_back(recipient);
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::cc(std::vector<MailAddress> recipients)
  {
    message_.email_recipients.cc.insert(
        message_.email_recipients.cc.end(),
        std::make_move_iterator(recipients.begin()),
        std::make_move_iterator(recipients.end()));
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::bcc(const MailAddress& recipient)
  {
    message_.email_recipients.bcc.emplace_back(recipient);
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::bcc(std::vector<MailAddress> recipients)
  {
    message_.email_recipients.bcc.insert(
        message_.email_recipients.bcc.end(),
        std::make_move_iterator(recipients.begin()),
        std::make_move_iterator(recipients.end()));
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::subject(std::string_view subject)
  {
    message_.subject = subject;
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::body(std::string_view body)
  {
    message_.text_body = body;
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::addAttachment(const MailAttachment& attachment)
  {
    message_.attachments_.emplace_back(attachment);
    return *this;
  }

  MailMessageBuilder& MailMessageBuilder::replyTo(
      const std::string& originalMessageId,
      const std::vector<std::string>& existingReferences)
  {
    // Set In-Reply-To header
    message_.email_threading.in_reply_to = originalMessageId;

    // Build References chain: existing references + original message-id
    message_.email_threading.references = existingReferences;
    message_.email_threading.references.push_back(originalMessageId);

    return *this;
  }

  [[nodiscard]] std::expected<MailMessage, std::string> MailMessageBuilder::build() const
  {
    if (message_.from.getAddress().empty())
    {
      return std::unexpected("From address is not set");
    }
    if (message_.email_recipients.to.empty() && message_.email_recipients.cc.empty() &&
        message_.email_recipients.bcc.empty())
    {
      return std::unexpected("At least one recipient is required");
    }
    return message_;
  }
}  // namespace aurora::mail::common::mail