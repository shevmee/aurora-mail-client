#ifndef CACHED_MESSAGE_HPP
#define CACHED_MESSAGE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace aurora::mail::common::message_cache
{

  /**
   * @brief Attachment metadata for cached messages.
   */
  struct CachedAttachment
  {
    std::string filename;      ///< Attachment filename
    std::string content_type;  ///< MIME type (e.g., "application/pdf")
    std::string part_id;       ///< IMAP body part ID (e.g., "1.2")
    uint32_t size = 0;         ///< Size in bytes
    std::string content_id;    ///< Content-ID for inline attachments
    bool is_inline = false;    ///< True if inline (embedded in HTML)
  };

  /**
   * @brief Cached message with full content for efficient local access.
   *
   * Stores essential message info to avoid repeated FETCH commands.
   * Supports CONDSTORE (RFC 7162) with per-message MODSEQ tracking.
   */
  struct CachedMessage
  {
    // === Core IMAP metadata ===
    uint32_t uid = 0;     ///< Unique identifier (persistent across sessions)
    uint32_t size = 0;    ///< RFC822.SIZE of the message
    uint64_t modseq = 0;  ///< MODSEQ for CONDSTORE change tracking
    std::string flags;    ///< Current flags (e.g., "\\Seen \\Flagged")

    // === Envelope data (from ENVELOPE fetch) ===
    std::string from;         ///< Envelope FROM field
    std::string to;           ///< Envelope TO field (may have multiple)
    std::string cc;           ///< Envelope CC field
    std::string bcc;          ///< Envelope BCC field
    std::string subject;      ///< Envelope SUBJECT field
    std::string date;         ///< Envelope DATE field
    std::string message_id;   ///< Message-ID header for threading
    std::string in_reply_to;  ///< In-Reply-To header for threading
    std::string references;   ///< References header (space-separated message-ids)

    // === Content type info ===
    std::string content_type;  ///< Primary content type (e.g., "text/plain",
                               ///< "multipart/mixed")

    // === Body content (populated by BODY[] or BODY[TEXT] fetch) ===
    std::string text_body;    ///< Plain text body content
    std::string html_body;    ///< HTML body content
    std::string preview;      ///< Short preview text (first ~200 chars, for list view)
    std::string raw_headers;  ///< Raw headers (from BODY[HEADER] or RFC822.HEADER)

    // === Attachments ===
    std::vector<CachedAttachment> attachments;

    // === Cache state ===
    bool has_envelope = false;   ///< Whether envelope data is cached
    bool has_body = false;       ///< Whether body content is cached
    bool has_structure = false;  ///< Whether BODYSTRUCTURE was parsed

    // === Flag helpers ===
    bool hasFlag(const std::string& flag) const
    {
      return flags.find(flag) != std::string::npos;
    }

    bool isSeen() const
    {
      return hasFlag("\\Seen");
    }
    bool isFlagged() const
    {
      return hasFlag("\\Flagged");
    }
    bool isAnswered() const
    {
      return hasFlag("\\Answered");
    }
    bool isDeleted() const
    {
      return hasFlag("\\Deleted");
    }
    bool isDraft() const
    {
      return hasFlag("\\Draft");
    }

    // === Content helpers ===
    bool hasTextBody() const
    {
      return !text_body.empty();
    }
    bool hasHtmlBody() const
    {
      return !html_body.empty();
    }
    bool hasAttachments() const
    {
      return !attachments.empty();
    }

    /**
     * @brief Get best available body for display.
     * @return HTML body if available, otherwise text body
     */
    const std::string& getDisplayBody() const
    {
      return html_body.empty() ? text_body : html_body;
    }

    /**
     * @brief Generate preview from body content.
     * @param max_length Maximum preview length
     */
    void generatePreview(size_t max_length = 200)
    {
      const std::string& source = text_body.empty() ? html_body : text_body;
      if (source.empty())
      {
        preview.clear();
        return;
      }

      // For HTML, we'd ideally strip tags - for now just truncate
      if (source.length() <= max_length)
      {
        preview = source;
      }
      else
      {
        preview = source.substr(0, max_length);
        // Try to break at word boundary
        size_t last_space = preview.rfind(' ');
        if (last_space > max_length / 2)
        {
          preview = preview.substr(0, last_space);
        }
        preview += "...";
      }
    }
  };

}  // namespace aurora::mail::common::message_cache

#endif  // CACHED_MESSAGE_HPP