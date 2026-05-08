#ifndef MIME_READER_HPP
#define MIME_READER_HPP

#include <MailAddress.hpp>
#include <MimeParseError.hpp>
#include <ReceivedMailMessage.hpp>
#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::mail::common::mime
{
  namespace reader
  {

    /**
     * @brief Parse a complete MIME message from raw RFC 5322 data.
     *
     * This is the primary function for parsing emails fetched via IMAP BODY[].
     * It handles:
     * - Header parsing and decoding (RFC 2047 encoded words)
     * - Multipart message traversal
     * - Text/HTML body extraction with charset conversion
     * - Attachment extraction with Base64/Quoted-Printable decoding
     *
     * @param raw_message Complete raw email message (headers + body)
     * @return Parsed ReceivedMailMessage or error
     */
    std::expected<mail::ReceivedMailMessage, MimeParseError> parseMessage(std::string_view raw_message);

    /**
     * @brief Parse only headers from a raw message.
     *
     * Faster than full parsing when you only need header info.
     * Useful for building message lists/previews.
     *
     * @param raw_message Raw email data (can be just headers or full message)
     * @return Partially filled ReceivedMailMessage (only headers populated)
     */
    std::expected<mail::ReceivedMailMessage, MimeParseError> parseHeaders(std::string_view raw_message);

    /**
     * @brief Decode RFC 2047 encoded words in a header value.
     *
     * Handles strings like "=?UTF-8?B?SGVsbG8gV29ybGQ=?=" or
     * "=?ISO-8859-1?Q?Hello_World?=".
     *
     * @param encoded The encoded header value
     * @return Decoded UTF-8 string
     */
    std::string decodeHeaderValue(std::string_view encoded);

    /**
     * @brief Decode content based on Content-Transfer-Encoding.
     *
     * @param encoded Encoded content
     * @param encoding Encoding type: "base64", "quoted-printable", "7bit", "8bit"
     * @return Decoded content
     */
    std::expected<std::string, MimeParseError> decodeContent(std::string_view encoded, std::string_view encoding);

    /**
     * @brief Convert text from specified charset to UTF-8.
     *
     * @param content Text content in source charset
     * @param charset Source charset (e.g., "ISO-8859-1", "windows-1252", "UTF-8")
     * @return UTF-8 encoded string
     */
    std::expected<std::string, MimeParseError> convertToUtf8(std::string_view content, std::string_view charset);

    /**
     * @brief Parse RFC 2822 date string into time_point.
     *
     * Handles formats like "Tue, 1 Jul 2003 10:52:37 +0200".
     *
     * @param date_str Date string from Date header
     * @return Parsed time_point or nullopt if parsing failed
     */
    std::optional<std::chrono::system_clock::time_point> parseDate(std::string_view date_str);

    /**
     * @brief Parse an address header (From, To, Cc, etc.) into MailAddress list.
     *
     * Handles formats like:
     * - "user@example.com"
     * - "User Name <user@example.com>"
     * - "user@example.com, other@example.com"
     *
     * @param header_value Raw header value
     * @return Vector of parsed addresses
     */
    std::vector<mail::MailAddress> parseAddressList(std::string_view header_value);

    /**
     * @brief Extract a single address from a header value.
     *
     * Convenience wrapper for parseAddressList when only one address is expected.
     *
     * @param header_value Raw header value
     * @return First address found, or empty MailAddress if none
     */
    mail::MailAddress parseAddress(std::string_view header_value);

  }  // namespace reader
}  // namespace aurora::mail::common::mime

#endif  // MIME_READER_HPP
