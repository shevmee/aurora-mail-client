#ifndef CORE_EMAIL_PARSER_HPP
#define CORE_EMAIL_PARSER_HPP

#include <QString>
#include <QVector>
#include <QDateTime>
#include <tuple>
#include <string>
#include <optional>
#include <MimeReader.hpp>
#include <ReceivedMailMessage.hpp>

namespace aurora::mail::app::email {

/**
 * @struct AttachmentInfo
 * @brief Qt-friendly attachment metadata for display.
 */
struct AttachmentInfo {
    QString filename;     ///< Original filename
    QString contentType;  ///< MIME type
    qint64 size;          ///< Size in bytes
    QByteArray data;      ///< Binary content
    bool isInline;        ///< True if embedded in HTML
    
    AttachmentInfo() : size(0), isInline(false) {}
};

/**
 * @struct ParsedEmailContent
 * @brief Full email content for display, including attachments.
 */
struct ParsedEmailContent {
    QString subject;
    QString from;
    QString body;         ///< HTML or formatted text body
    bool isHtml = false;
    QVector<AttachmentInfo> attachments;
    
    bool isValid() const { return !subject.isEmpty() || !from.isEmpty() || !body.isEmpty(); }
};

/**
 * @struct EmailSummary
 * @brief Qt-friendly email summary for display in the mail list.
 * 
 * This is a Qt wrapper around the library's ReceivedMailMessage,
 * providing QString types for easy UI integration.
 */
struct EmailSummary {
    QString uid;        ///< Unique identifier from IMAP
    QString messageId;  ///< Message-ID header for threading
    QString from;       ///< Sender name/email
    QString subject;    ///< Email subject
    QString preview;    ///< Short preview of body
    QDateTime date;     ///< Date received
    bool isRead = false; ///< Read/unread status
    bool hasAttachments = false; ///< Whether email has attachments
    
    EmailSummary() = default;
    EmailSummary(const EmailSummary&) = default;
    EmailSummary& operator=(const EmailSummary&) = default;
    EmailSummary(EmailSummary&&) noexcept = default;
    EmailSummary& operator=(EmailSummary&&) noexcept = default;
    ~EmailSummary() = default;
    
    /**
     * @brief Construct from library's ReceivedMailMessage.
     */
    static EmailSummary fromReceivedMessage(
        const aurora::mail::common::mail::ReceivedMailMessage& msg,
        const QString& uid);
};

/**
 * @class EmailParser
 * @brief Qt wrapper around AuroraMailEngine (MIME parser).
 *
 * Provides Qt-friendly interfaces for email parsing, delegating
 * the actual MIME parsing to the library's robust GMime-based implementation.
 * 
 * This class is intentionally thin - all complex MIME parsing is handled
 * by the library's MimeReader which uses GMime for robust handling of:
 * - RFC 2047 encoded headers
 * - Multipart messages (mixed, alternative, related)
 * - Base64 and quoted-printable content transfer encoding
 * - Charset conversion to UTF-8
 * - Attachment extraction
 */
class EmailParser {
public:
    /**
     * @brief Parse IMAP FETCH response to extract email summaries.
     * 
     * Parses the ENVELOPE data from IMAP responses for list display.
     * For full content parsing, use parseFullEmailContent().
     * 
     * @param response Raw IMAP FETCH response string.
     * @return Vector of EmailSummary objects.
     */
    static QVector<EmailSummary> parseEmailList(const std::string& response);

    /**
     * @brief Parse full email content from IMAP FETCH response.
     * 
     * Uses the library's MimeParser for robust MIME handling.
     * 
     * @param response Raw IMAP FETCH response with BODY[].
     * @return Tuple of (subject, from, body as HTML).
     * @deprecated Use parseFullEmailContent() for attachment support.
     */
    static std::tuple<QString, QString, QString> parseEmailContent(const std::string& response);

    /**
     * @brief Parse full email content including attachments.
     * 
     * @param response Raw IMAP FETCH response with BODY[].
     * @return ParsedEmailContent with body and attachments.
     */
    static ParsedEmailContent parseFullEmailContent(const std::string& response);

    /**
     * @brief Parse raw MIME message using the library's parser.
     * 
     * @param rawMessage Complete raw email (headers + body).
     * @return Parsed ReceivedMailMessage or nullopt on error.
     */
    static std::optional<aurora::mail::common::mail::ReceivedMailMessage> 
    parseRawMessage(const std::string& rawMessage);

    /**
     * @brief Format plain text as styled HTML for display.
     * @param plainText Plain text content.
     * @return HTML-formatted string with links and styling.
     */
    static QString formatPlainTextAsHtml(const QString& plainText);

private:
    EmailParser() = default; // Static class

    /**
     * @brief Extract raw body from IMAP FETCH response.
     * 
     * Handles IMAP literal syntax: BODY[] {size}\r\n<content>
     * 
     * @param response Full IMAP response.
     * @return Raw message body or empty string.
     */
    static std::string extractBodyFromFetch(const std::string& response);
};

} // namespace aurora::mail::app::email

#endif // CORE_EMAIL_PARSER_HPP
