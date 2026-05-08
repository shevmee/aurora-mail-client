#include <gmime/gmime.h>

#include <MimeContext.hpp>
#include <MimeReader.hpp>
#include <ReceivedMailMessage.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace aurora::mail::common::mime
{
  namespace
  {

    using mail::MailAddress;
    using mail::ParsedAttachment;
    using mail::ReceivedMailMessage;

    // Convert GMime address to MailAddress
    MailAddress gmimeToMailAddress(InternetAddress* addr)
    {
      if (!addr || !INTERNET_ADDRESS_IS_MAILBOX(addr))
      {
        return {};
      }

      auto* mailbox = INTERNET_ADDRESS_MAILBOX(addr);
      const char* email = internet_address_mailbox_get_addr(mailbox);
      const char* name = internet_address_get_name(addr);

      return MailAddress::create(email, name).value_or(MailAddress{});
    }

    // Extract addresses from GMime address list
    std::vector<MailAddress> extractAddresses(InternetAddressList* list)
    {
      std::vector<MailAddress> result;
      if (list == nullptr)
        return result;

      int count = internet_address_list_length(list);
      for (int i = 0; i < count; ++i)
      {
        InternetAddress* addr = internet_address_list_get_address(list, i);

        if (INTERNET_ADDRESS_IS_GROUP(addr))
        {
          auto* group = INTERNET_ADDRESS_GROUP(addr);
          InternetAddressList* members = internet_address_group_get_members(group);
          auto group_addrs = extractAddresses(members);
          result.insert(result.end(), group_addrs.begin(), group_addrs.end());
        }
        else
        {
          auto mail_addr = gmimeToMailAddress(addr);
          if (mail_addr.isValid())
          {
            result.push_back(mail_addr);
          }
        }
      }

      return result;
    }

    // Lowercase string helper
    std::string toLower(std::string_view str)
    {
      std::string result(str);
      std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
      return result;
    }

    // Forward declaration for recursive part processing
    void processMimePart(GMimeObject* part, ReceivedMailMessage& msg);

    // Process multipart containers
    void processMultipart(GMimeMultipart* multipart, ReceivedMailMessage& msg)
    {
      int count = g_mime_multipart_get_count(multipart);

      for (int i = 0; i < count; ++i)
      {
        GMimeObject* part = g_mime_multipart_get_part(multipart, i);
        processMimePart(part, msg);
      }
    }

    // Extract text content from a MIME part
    std::string extractTextContent(GMimePart* part)
    {
      GMimeDataWrapper* wrapper = g_mime_part_get_content(part);
      if (!wrapper)
        return "";

      GMimeStream* stream = g_mime_stream_mem_new();
      g_mime_data_wrapper_write_to_stream(wrapper, stream);

      GByteArray* array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
      std::string content(reinterpret_cast<const char*>(array->data), array->len);

      g_object_unref(stream);

      // Get charset and convert to UTF-8 if needed
      GMimeContentType* content_type = g_mime_object_get_content_type(GMIME_OBJECT(part));
      const char* charset = g_mime_content_type_get_parameter(content_type, "charset");

      if (charset && strcasecmp(charset, "utf-8") != 0 && strcasecmp(charset, "us-ascii") != 0)
      {
        GError* error = nullptr;
        gchar* converted =
            g_convert(content.data(), static_cast<long>(content.size()), "UTF-8", charset, nullptr, nullptr, &error);
        if (converted)
        {
          content = converted;
          g_free(converted);
        }
        if (error)
        {
          g_error_free(error);
        }
      }

      return content;
    }

    // Extract attachment data from a MIME part
    ParsedAttachment extractAttachment(GMimePart* part)
    {
      ParsedAttachment attachment;

      const char* filename = g_mime_part_get_filename(part);
      attachment.filename = filename ? filename : "unnamed";

      GMimeContentType* content_type = g_mime_object_get_content_type(GMIME_OBJECT(part));
      if (content_type)
      {
        char* type_str = g_mime_content_type_get_mime_type(content_type);
        if (type_str)
        {
          attachment.content_type = type_str;
          g_free(type_str);
        }
      }

      // Content-ID is used for inline images referenced in HTML via cid: URLs.
      // UI should replace <img src="cid:xyz"> with actual image data.
      const char* content_id = g_mime_object_get_content_id(GMIME_OBJECT(part));
      if (content_id)
      {
        attachment.content_id = content_id;
      }

      GMimeContentDisposition* disposition = g_mime_object_get_content_disposition(GMIME_OBJECT(part));
      if (disposition)
      {
        const char* disp_str = g_mime_content_disposition_get_disposition(disposition);
        attachment.is_inline = disp_str && strcasecmp(disp_str, "inline") == 0;
      }

      GMimeDataWrapper* wrapper = g_mime_part_get_content(part);
      if (wrapper)
      {
        GMimeStream* stream = g_mime_stream_mem_new();
        g_mime_data_wrapper_write_to_stream(wrapper, stream);

        GByteArray* array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
        attachment.data.assign(array->data, array->data + array->len);

        g_object_unref(stream);
      }

      return attachment;
    }

    // Process a single MIME part (recursive)
    void processMimePart(GMimeObject* part, ReceivedMailMessage& msg)
    {
      if (!part)
        return;

      GMimeContentType* content_type = g_mime_object_get_content_type(part);
      if (!content_type)
        return;

      const char* type = g_mime_content_type_get_media_type(content_type);
      const char* subtype = g_mime_content_type_get_media_subtype(content_type);

      if (!type)
        return;

      if (GMIME_IS_MULTIPART(part))
      {
        processMultipart(GMIME_MULTIPART(part), msg);
      }
      else if (GMIME_IS_PART(part))
      {
        GMimePart* mime_part = GMIME_PART(part);

        GMimeContentDisposition* disposition = g_mime_object_get_content_disposition(part);
        const char* disp_str = disposition ? g_mime_content_disposition_get_disposition(disposition) : nullptr;

        bool is_attachment = disp_str && strcasecmp(disp_str, "attachment") == 0;

        if (strcasecmp(type, "text") == 0 && !is_attachment)
        {
          std::string content = extractTextContent(mime_part);

          // Store both text and HTML versions if present (multipart/alternative).
          // UI should use ReceivedMailMessage::preferredBody() to select the best
          // format (HTML if available, otherwise plain text).
          if (strcasecmp(subtype, "plain") == 0 && msg.text_body.empty())
          {
            msg.text_body = std::move(content);
          }
          else if (strcasecmp(subtype, "html") == 0 && msg.html_body.empty())
          {
            msg.html_body = std::move(content);
          }
        }
        else
        {
          auto attachment = extractAttachment(mime_part);
          if (!attachment.empty())
          {
            msg.attachments.push_back(std::move(attachment));
          }
        }
      }
      else if (GMIME_IS_MESSAGE_PART(part))
      {
        GMimeMessagePart* msg_part = GMIME_MESSAGE_PART(part);
        GMimeMessage* embedded = g_mime_message_part_get_message(msg_part);

        if (embedded)
        {
          ParsedAttachment attachment;
          attachment.filename = "message.eml";
          attachment.content_type = "message/rfc822";

          GMimeStream* stream = g_mime_stream_mem_new();
          g_mime_object_write_to_stream(GMIME_OBJECT(embedded), nullptr, stream);

          GByteArray* array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
          attachment.data.assign(array->data, array->data + array->len);

          g_object_unref(stream);

          msg.attachments.push_back(std::move(attachment));
        }
      }
    }

    // Extract all headers into the map
    void extractHeaders(GMimeMessage* message, ReceivedMailMessage& msg)
    {
      GMimeHeaderList* headers = g_mime_object_get_header_list(GMIME_OBJECT(message));
      if (!headers)
        return;

      int count = g_mime_header_list_get_count(headers);
      for (int i = 0; i < count; ++i)
      {
        GMimeHeader* header = g_mime_header_list_get_header_at(headers, i);
        if (!header)
          continue;

        const char* name = g_mime_header_get_name(header);
        const char* value = g_mime_header_get_value(header);

        if (name && value)
        {
          msg.headers[toLower(name)] = value;
        }
      }
    }

  }  // anonymous namespace

  namespace reader
  {

    std::expected<mail::ReceivedMailMessage, MimeParseError> parseMessage(std::string_view raw_message)
    {
      if (raw_message.empty())
      {
        return std::unexpected(MimeParseError{ MimeParseError::Type::EmptyMessage, "Empty message data" });
      }

      getMimeContext();

      GMimeStream* stream = g_mime_stream_mem_new_with_buffer(raw_message.data(), raw_message.size());

      GMimeParser* parser = g_mime_parser_new_with_stream(stream);
      GMimeMessage* message = g_mime_parser_construct_message(parser, nullptr);

      g_object_unref(parser);
      g_object_unref(stream);

      if (!message)
      {
        return std::unexpected(MimeParseError{ MimeParseError::Type::InvalidFormat, "Failed to parse message structure" });
      }

      ReceivedMailMessage result;

      const char* subject = g_mime_message_get_subject(message);
      result.subject = subject ? subject : "";

      const char* message_id = g_mime_message_get_message_id(message);
      result.email_threading.message_id = message_id ? message_id : "";

      const char* date_header_str = g_mime_object_get_header(GMIME_OBJECT(message), "Date");
      if (date_header_str)
      {
        auto parsed_date = parseDate(date_header_str);

        if (parsed_date.has_value())
        {
          result.date = parsed_date.value();
          result.has_date = true;
        }
      }

      InternetAddressList* from_list = g_mime_message_get_from(message);
      if (from_list && internet_address_list_length(from_list) > 0)
      {
        auto from_addrs = extractAddresses(from_list);
        if (!from_addrs.empty())
        {
          result.from = from_addrs[0];
        }
      }

      InternetAddressList* reply_to_list = g_mime_message_get_reply_to(message);
      if (reply_to_list && internet_address_list_length(reply_to_list) > 0)
      {
        auto reply_to_addrs = extractAddresses(reply_to_list);
        if (!reply_to_addrs.empty())
        {
          result.reply_to = reply_to_addrs[0];
        }
      }

      InternetAddressList* to_list = g_mime_message_get_to(message);
      result.email_recipients.to = extractAddresses(to_list);

      InternetAddressList* cc_list = g_mime_message_get_cc(message);
      result.email_recipients.cc = extractAddresses(cc_list);

      extractHeaders(message, result);

      auto in_reply_to_str = result.getHeader("in-reply-to");
      if (!in_reply_to_str.empty())
      {
        result.email_threading.in_reply_to = in_reply_to_str;
      }

      auto references_str = result.getHeader("references");
      if (!references_str.empty())
      {
        // Split references by whitespace
        std::istringstream iss(references_str);
        std::string ref;
        while (iss >> ref)
        {
          result.email_threading.references.push_back(ref);
        }
      }

      GMimeObject* mime_part = g_mime_message_get_mime_part(message);
      if (mime_part)
      {
        processMimePart(mime_part, result);
      }

      g_object_unref(message);

      return result;
    }

    std::expected<mail::ReceivedMailMessage, MimeParseError> parseHeaders(std::string_view raw_message)
    {
      if (raw_message.empty())
      {
        return std::unexpected(MimeParseError{ MimeParseError::Type::EmptyMessage, "Empty message data" });
      }

      getMimeContext();

      auto header_end = raw_message.find("\r\n\r\n");
      if (header_end == std::string_view::npos)
      {
        header_end = raw_message.find("\n\n");
      }

      std::string_view headers_only =
          (header_end != std::string_view::npos) ? raw_message.substr(0, header_end + 4) : raw_message;

      GMimeStream* stream = g_mime_stream_mem_new_with_buffer(headers_only.data(), headers_only.size());

      GMimeParser* parser = g_mime_parser_new_with_stream(stream);
      GMimeMessage* message = g_mime_parser_construct_message(parser, nullptr);

      g_object_unref(parser);
      g_object_unref(stream);

      if (!message)
      {
        return std::unexpected(MimeParseError{ MimeParseError::Type::InvalidFormat, "Failed to parse headers" });
      }

      ReceivedMailMessage result;

      const char* subject = g_mime_message_get_subject(message);
      result.subject = subject ? subject : "";

      const char* message_id = g_mime_message_get_message_id(message);
      result.email_threading.message_id = message_id ? message_id : "";

      GDateTime* date = g_mime_message_get_date(message);
      if (date)
      {
        result.date = std::chrono::system_clock::from_time_t(g_date_time_to_unix(date));
        result.has_date = true;
      }

      InternetAddressList* from_list = g_mime_message_get_from(message);
      if (from_list && internet_address_list_length(from_list) > 0)
      {
        auto from_addrs = extractAddresses(from_list);
        if (!from_addrs.empty())
        {
          result.from = from_addrs[0];
        }
      }

      InternetAddressList* to_list = g_mime_message_get_to(message);
      result.email_recipients.to = extractAddresses(to_list);

      InternetAddressList* cc_list = g_mime_message_get_cc(message);
      result.email_recipients.cc = extractAddresses(cc_list);

      extractHeaders(message, result);

      auto in_reply_to_str = result.getHeader("in-reply-to");
      if (!in_reply_to_str.empty())
      {
        result.email_threading.in_reply_to = in_reply_to_str;
      }

      auto references_str = result.getHeader("references");
      if (!references_str.empty())
      {
        // Split references by whitespace
        std::istringstream iss(references_str);
        std::string ref;
        while (iss >> ref)
        {
          result.email_threading.references.push_back(ref);
        }
      }

      g_object_unref(message);

      return result;
    }

    std::string decodeHeaderValue(std::string_view encoded)
    {
      getMimeContext();

      char* decoded = g_mime_utils_header_decode_text(nullptr, std::string(encoded).c_str());

      if (!decoded)
      {
        return std::string(encoded);
      }

      std::string result(decoded);
      g_free(decoded);
      return result;
    }

    std::expected<std::string, MimeParseError> decodeContent(std::string_view encoded, std::string_view encoding)
    {
      getMimeContext();

      std::string encoding_lower = toLower(encoding);

      if (encoding_lower == "base64")
      {
        gsize out_len = 0;
        guint8* decoded = g_base64_decode(std::string(encoded).c_str(), &out_len);
        if (!decoded)
        {
          return std::unexpected(MimeParseError{ MimeParseError::Type::EncodingError, "Failed to decode base64 content" });
        }
        std::string result(reinterpret_cast<char*>(decoded), out_len);
        g_free(decoded);
        return result;
      }
      else if (encoding_lower == "quoted-printable")
      {
        std::string result;
        result.reserve(encoded.size());

        for (size_t i = 0; i < encoded.size(); ++i)
        {
          if (encoded[i] == '=' && i + 2 < encoded.size())
          {
            if (encoded[i + 1] == '\r' && encoded[i + 2] == '\n')
            {
              i += 2;
              continue;
            }
            else if (encoded[i + 1] == '\n')
            {
              i += 1;
              continue;
            }
            char hex[3] = { encoded[i + 1], encoded[i + 2], '\0' };
            char* end = nullptr;
            long value = strtol(hex, &end, 16);
            if (end == hex + 2)
            {
              result += static_cast<char>(value);
              i += 2;
              continue;
            }
          }
          result += encoded[i];
        }
        return result;
      }
      else if (encoding_lower == "7bit" || encoding_lower == "8bit" || encoding_lower == "binary" || encoding_lower.empty())
      {
        return std::string(encoded);
      }

      return std::unexpected(
          MimeParseError{ MimeParseError::Type::EncodingError, "Unknown encoding: " + std::string(encoding) });
    }

    std::expected<std::string, MimeParseError> convertToUtf8(std::string_view content, std::string_view charset)
    {
      getMimeContext();

      std::string charset_lower = toLower(charset);

      if (charset_lower == "utf-8" || charset_lower == "us-ascii" || charset_lower.empty())
      {
        return std::string(content);
      }

      GError* error = nullptr;
      gsize bytes_read = 0;
      gsize bytes_written = 0;

      gchar* converted = g_convert(
          content.data(),
          static_cast<long>(content.size()),
          "UTF-8",
          std::string(charset).c_str(),
          &bytes_read,
          &bytes_written,
          &error);

      if (error)
      {
        std::string error_msg = error->message;
        g_error_free(error);
        return std::unexpected(
            MimeParseError{ MimeParseError::Type::CharsetError, "Charset conversion failed: " + error_msg });
      }

      std::string result(converted, bytes_written);
      g_free(converted);
      return result;
    }

    std::optional<std::chrono::system_clock::time_point> parseDate(std::string_view date_str)
    {
      getMimeContext();

      GDateTime* date = g_mime_utils_header_decode_date(std::string(date_str).c_str());
      if (!date)
      {
        return std::nullopt;
      }

      auto result = std::chrono::system_clock::from_time_t(g_date_time_to_unix(date));
      g_date_time_unref(date);
      return result;
    }

    std::vector<mail::MailAddress> parseAddressList(std::string_view header_value)
    {
      getMimeContext();

      InternetAddressList* list = internet_address_list_parse(nullptr, std::string(header_value).c_str());

      if (!list)
      {
        return {};
      }

      auto result = extractAddresses(list);
      g_object_unref(list);
      return result;
    }

    mail::MailAddress parseAddress(std::string_view header_value)
    {
      auto addresses = parseAddressList(header_value);
      return addresses.empty() ? mail::MailAddress{} : addresses[0];
    }

  }  // namespace reader
}  // namespace aurora::mail::common::mime
