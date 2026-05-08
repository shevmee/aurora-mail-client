#ifndef MIME_WRITER_HPP
#define MIME_WRITER_HPP

#include <MailMessage.hpp>
#include <string>

namespace aurora::mail::common::mime
{
  namespace writer
  {

    /**
     * @brief Build a complete MIME message from a MailMessage structure.
     *
     * Generates a properly formatted RFC 5322/MIME message including:
     * - All standard headers (From, To, CC, BCC, Subject, Date, Message-ID)
     * - Reply-To header if specified
     * - multipart/mixed for attachments
     * - Proper Content-Type and Content-Transfer-Encoding
     *
     * @param message The message to serialize
     * @param hide_bcc If true, BCC header is excluded from output (use for SMTP).
     *                 If false, BCC is included (use for saving to Sent folder).
     *                 Default: true (safe for sending)
     * @return Complete MIME message as a string
     */
    std::string buildMimeMessage(const mail::MailMessage& message, bool hide_bcc = true);

  }  // namespace writer
}  // namespace aurora::mail::common::mime

#endif  // MIME_WRITER_HPP
