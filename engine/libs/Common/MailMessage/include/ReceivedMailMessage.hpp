#ifndef RECEIVED_MAIL_MESSAGE_HPP
#define RECEIVED_MAIL_MESSAGE_HPP

#include <MailAddress.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "EmailRecipients.hpp"
#include "EmailThreading.hpp"

namespace aurora::mail::common::mail
{

  struct EmailFlags
  {
    bool seen = false;
    bool answered = false;
    bool flagged = false;
    bool deleted = false;
    bool draft = false;
  };

  /**
   * @brief Represents a parsed attachment from a received email.
   *
   * @note Memory limitation: Attachment data is loaded entirely into RAM.
   *       For very large attachments (e.g., 2GB), this may exhaust memory.
   */
  struct ParsedAttachment
  {
    std::string filename;       ///< Original filename
    std::string content_type;   ///< MIME type (e.g., "image/png")
    std::string content_id;     ///< Content-ID for inline images (use with cid: URLs in HTML)
    std::vector<uint8_t> data;  ///< Decoded binary content (loaded in memory)
    bool is_inline = false;     ///< True if inline (embedded in HTML body)

    /**
     * @brief Get file size in bytes.
     */
    size_t size() const
    {
      return data.size();
    }

    /**
     * @brief Check if attachment has data.
     */
    bool empty() const
    {
      return data.empty();
    }
  };

  /**
   * @brief Represents a fully parsed received email message.
   *
   * This struct is the result of parsing raw MIME data fetched via IMAP.
   * It contains all extracted headers, body content, and attachments.
   */
  struct ReceivedMailMessage
  {
    // === ENVELOPE HEADERS ===

    EmailThreading email_threading;

    MailAddress from;  ///< Sender address

    EmailRecipients email_recipients;

    MailAddress reply_to;  ///< Reply-To address (if different from From)

    std::string subject;  ///< Decoded subject line

    std::chrono::system_clock::time_point date;  ///< Parsed date
    bool has_date = false;                       ///< True if date was successfully parsed

    // === BODY CONTENT ===

    std::string text_body;  ///< Plain text body (text/plain)
    std::string html_body;  ///< HTML body (text/html) - empty if not present

    /**
     * @brief Check if message has HTML content.
     */
    bool hasHtmlBody() const
    {
      return !html_body.empty();
    }

    /**
     * @brief Get preferred body (HTML if available, otherwise text).
     */
    const std::string& preferredBody() const
    {
      return hasHtmlBody() ? html_body : text_body;
    }

    // === ATTACHMENTS ===

    std::vector<ParsedAttachment> attachments;  ///< File attachments

    /**
     * @brief Check if message has attachments.
     */
    bool hasAttachments() const
    {
      return !attachments.empty();
    }

    /**
     * @brief Get total attachment count.
     */
    size_t attachmentCount() const
    {
      return attachments.size();
    }

    // === RAW HEADERS ===

    /**
     * @brief All parsed headers as key-value pairs.
     *
     * Keys are lowercase for consistent lookup.
     * Use this for accessing custom or less common headers.
     */
    std::unordered_map<std::string, std::string> headers;

    /**
     * @brief Get a header value by name (case-insensitive).
     *
     * @param name Header name (e.g., "X-Priority", "List-Unsubscribe")
     * @return Header value or empty string if not found
     */
    std::string getHeader(const std::string& name) const;

    /**
     * @brief Check if a header exists.
     */
    bool hasHeader(const std::string& name) const;

    EmailFlags flags;

    // === METADATA ===

    uint32_t uid = 0;              ///< IMAP UID (0 if unknown)
    uint32_t sequence_number = 0;  ///< IMAP sequence number (0 if unknown)
    size_t size_bytes = 0;         ///< RFC822.SIZE
  };

}  // namespace aurora::mail::common::mail

#endif  // RECEIVED_MAIL_MESSAGE_HPP
