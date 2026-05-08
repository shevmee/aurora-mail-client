#ifndef MAIL_MESSAGE_BUILDER_HPP
#define MAIL_MESSAGE_BUILDER_HPP

#include <MailAddress.hpp>
#include <MailAttachment.hpp>
#include <MailMessage.hpp>
#include <expected>
#include <string>
#include <vector>

namespace aurora::mail::common::mail
{
  /**
   * @brief A fluent builder for constructing MailMessage objects.
   *
   * The `MailMessageBuilder` class provides a step-by-step way to build a
   * complete `MailMessage` using chainable methods. This is useful for ensuring
   * the message is built consistently and clearly.
   */
  class MailMessageBuilder
  {
   public:
    MailMessageBuilder() = default;

    /**
     * @brief Sets the sender's email address.
     *
     * @param email The sender's email address.
     * @param name (Optional) The sender's display name.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& from(const MailAddress& sender);

    /**
     * @brief Adds a recipient to the "To" list.
     *
     * @param recipient The recipient's email address and name.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& to(const MailAddress& recipient);

    /**
     * @brief Adds multiple recipients to the "To" list.
     *
     * @param recipients The list of recipients' email addresses and names.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& to(std::vector<MailAddress> recipients);

    /**
     * @brief Adds a recipient to the "CC" list.
     *
     * @param recipient The recipient's email address and name.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& cc(const MailAddress& recipient);

    /**
     * @brief Adds multiple recipients to the "CC" list.
     *
     * @param recipients The list of recipients' email addresses and names.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& cc(std::vector<MailAddress> recipients);

    /**
     * @brief Adds a recipient to the "BCC" list.
     *
     * @param recipient The recipient's email address and name.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& bcc(const MailAddress& recipient);

    /**
     * @brief Adds multiple recipients to the "BCC" list.
     *
     * @param recipients The list of recipients' email addresses and names.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& bcc(std::vector<MailAddress> recipients);

    /**
     * @brief Sets the subject line of the email.
     *
     * @param subject The subject text.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& subject(std::string_view subject);

    /**
     * @brief Sets the body of the email.
     *
     * @param body The message content, typically plain text or HTML.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& body(std::string_view body);

    /**
     * @brief Adds a file attachment to the email.
     *
     * @param attachment The attachment to add.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& addAttachment(const MailAttachment& attachment);

    /**
     * @brief Adds multiple attachments to the email.
     *
     * @param attachments The list of attachments to add.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& addAttachments(std::vector<MailAttachment> attachments);

    /**
     * @brief Sets threading headers for a reply email.
     *
     * Sets the In-Reply-To header to the original message's Message-ID,
     * and builds the References chain for proper threading in email clients.
     *
     * @param originalMessageId The Message-ID of the email being replied to.
     * @param existingReferences (Optional) Existing References from the original
     * email.
     * @return Reference to the builder for chaining.
     */
    MailMessageBuilder& replyTo(
        const std::string& originalMessageId,
        const std::vector<std::string>& existingReferences = {});

    /**
     * @brief Builds the final MailMessage object using the provided data.
     *
     * @return A fully constructed `MailMessage` instance.
     */
    [[nodiscard]] std::expected<MailMessage, std::string> build() const;

   private:
    MailMessage message_;
  };

}  // namespace aurora::mail::common::mail

#endif  // MAIL_MESSAGE_BUILDER_HPP