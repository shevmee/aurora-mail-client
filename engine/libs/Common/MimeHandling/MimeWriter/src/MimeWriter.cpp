#include <gmime/gmime.h>

#include <MailAddress.hpp>
#include <MailAttachment.hpp>
#include <MailMessage.hpp>
#include <MimeContext.hpp>
#include <MimeWriter.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace aurora::mail::common::mime
{
  namespace
  {

    // Helper to detect MIME type using GIO
    std::string detectMimeType(const std::filesystem::path& path)
    {
      std::ifstream file(path, std::ios::binary);
      std::vector<guchar> buffer;

      if (file)
      {
        constexpr size_t kSniffSize = 4096;
        buffer.resize(kSniffSize);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        auto bytesRead = static_cast<size_t>(file.gcount());
        buffer.resize(bytesRead);
      }

      gboolean uncertain = FALSE;
      gchar* guessed_type =
          g_content_type_guess(path.filename().c_str(), buffer.empty() ? nullptr : buffer.data(), buffer.size(), &uncertain);

      if (guessed_type == nullptr)
      {
        return "application/octet-stream";
      }

      gchar* mime_type = g_content_type_get_mime_type(guessed_type);
      std::string result = mime_type ? mime_type : "application/octet-stream";

      g_free(guessed_type);
      g_free(mime_type);

      return result;
    }

    GMimePart* createTextPart(const std::string& text)
    {
      GMimeDataWrapper* wrapper = g_mime_data_wrapper_new_with_stream(
          g_mime_stream_mem_new_with_buffer(text.c_str(), text.length()), GMIME_CONTENT_ENCODING_DEFAULT);
      GMimePart* part = g_mime_part_new_with_type("text", "plain");
      g_mime_part_set_content(part, wrapper);
      g_mime_object_set_content_type_parameter(GMIME_OBJECT(part), "charset", "UTF-8");
      g_object_unref(wrapper);
      return part;
    }

    // Helper to create a GMimeStreamFile for streaming file access
    GMimeStream* createFileStream(const std::filesystem::path& path)
    {
      FILE* file = std::fopen(path.c_str(), "rb");
      if (!file)
      {
        return nullptr;
      }
      GMimeStream* stream = g_mime_stream_file_new(file);
      g_mime_stream_file_set_owner(GMIME_STREAM_FILE(stream), TRUE);
      return stream;
    }

    // Helper to set From address on a GMimeMessage
    void setFromAddress(GMimeMessage* mime_message, const mail::MailAddress& from)
    {
      const char* name = from.getName().empty() ? nullptr : from.getName().c_str();

      g_mime_message_add_mailbox(mime_message, GMIME_ADDRESS_TYPE_FROM, name, from.getAddress().c_str());
    }

    void setReplyTo(GMimeMessage* mime_message, const mail::MailAddress& reply_to)
    {
      if (!reply_to.isValid())
        return;

      InternetAddressList* list = g_mime_message_get_reply_to(mime_message);
      InternetAddress* addr = internet_address_mailbox_new(
          reply_to.getName().empty() ? nullptr : reply_to.getName().c_str(), reply_to.getAddress().c_str());
      internet_address_list_add(list, addr);
      g_object_unref(addr);
    }

    // Helper to set To addresses on a GMimeMessage
    void setToAddresses(GMimeMessage* mime_message, const std::vector<mail::MailAddress>& to)
    {
      for (const auto& addr : to)
      {
        const char* name = addr.getName().empty() ? nullptr : addr.getName().c_str();
        g_mime_message_add_mailbox(mime_message, GMIME_ADDRESS_TYPE_TO, name, addr.getAddress().c_str());
      }
    }

    // Helper to set CC addresses on a GMimeMessage
    void setCcAddresses(GMimeMessage* mime_message, const std::vector<mail::MailAddress>& cc)
    {
      for (const auto& addr : cc)
      {
        const char* name = addr.getName().empty() ? nullptr : addr.getName().c_str();
        g_mime_message_add_mailbox(mime_message, GMIME_ADDRESS_TYPE_CC, name, addr.getAddress().c_str());
      }
    }

    // Helper to set BCC addresses on a GMimeMessage
    void setBccAddresses(GMimeMessage* mime_message, const std::vector<mail::MailAddress>& bcc)
    {
      for (const auto& addr : bcc)
      {
        const char* name = addr.getName().empty() ? nullptr : addr.getName().c_str();
        g_mime_message_add_mailbox(mime_message, GMIME_ADDRESS_TYPE_BCC, name, addr.getAddress().c_str());
      }
    }

    // Helper to set subject on a GMimeMessage
    void setSubject(GMimeMessage* mime_message, const std::string& subject)
    {
      g_mime_message_set_subject(mime_message, subject.c_str(), "UTF-8");
    }

    void setDate(GMimeMessage* mime_message)
    {
      GDateTime* now = g_date_time_new_now_local();
      g_mime_message_set_date(mime_message, now);
      g_date_time_unref(now);
    }

    // Helper to set Message-ID (critical for anti-spam and threading)
    void setMessageId(GMimeMessage* mime_message, const std::string& domain)
    {
      std::string id_domain = domain.empty() ? "localhost" : domain;
      char* msg_id = g_mime_utils_generate_message_id(id_domain.c_str());
      g_mime_message_set_message_id(mime_message, msg_id);
      g_free(msg_id);
    }

    // Helper to set In-Reply-To header (RFC 5322 threading)
    void setInReplyTo(GMimeMessage* mime_message, const std::string& message_id)
    {
      if (message_id.empty())
        return;
      g_mime_object_set_header(GMIME_OBJECT(mime_message), "In-Reply-To", message_id.c_str(), nullptr);
    }

    // Helper to set References header (RFC 5322 threading)
    void setReferences(GMimeMessage* mime_message, const std::vector<std::string>& refs)
    {
      if (refs.empty())
        return;

      std::string refs_header;
      for (size_t i = 0; i < refs.size(); ++i)
      {
        if (i > 0)
          refs_header += " ";
        refs_header += refs[i];
      }

      g_mime_object_set_header(GMIME_OBJECT(mime_message), "References", refs_header.c_str(), nullptr);
    }

    // Helper to add attachment to multipart
    void addAttachment(GMimeMultipart* multipart, const mail::MailAttachment& attachment)
    {
      GMimeStream* file_stream = createFileStream(attachment.getPath());
      if (!file_stream)
      {
        std::cerr << "Warning: Failed to open attachment file: " << attachment.getPath().string() << '\n';
        return;
      }

      std::string mime_type_str = detectMimeType(attachment.getPath());

      // Parse MIME type into type/subtype
      std::string type = "application";
      std::string subtype = "octet-stream";
      size_t slash_pos = mime_type_str.find('/');
      if (slash_pos != std::string::npos)
      {
        type = mime_type_str.substr(0, slash_pos);
        subtype = mime_type_str.substr(slash_pos + 1);
      }

      GMimeDataWrapper* attach_wrapper = g_mime_data_wrapper_new_with_stream(file_stream, GMIME_CONTENT_ENCODING_DEFAULT);

      GMimePart* attach_part = g_mime_part_new_with_type(type.c_str(), subtype.c_str());
      g_mime_part_set_content(attach_part, attach_wrapper);
      g_mime_part_set_filename(attach_part, attachment.getName().c_str());
      g_mime_part_set_content_encoding(attach_part, GMIME_CONTENT_ENCODING_BASE64);

      GMimeContentDisposition* disposition = g_mime_content_disposition_new();
      g_mime_content_disposition_set_disposition(disposition, "attachment");
      g_mime_content_disposition_set_parameter(disposition, "filename", attachment.getName().c_str());
      g_mime_object_set_content_disposition(GMIME_OBJECT(attach_part), disposition);
      g_object_unref(disposition);

      g_object_unref(attach_wrapper);
      g_object_unref(file_stream);
      g_mime_multipart_add(multipart, GMIME_OBJECT(attach_part));
      g_object_unref(attach_part);
    }

    // Helper to set message body and attachments
    // Supports: plain text and attachments
    void setMessageBody(GMimeMessage* mime_message, const mail::MailMessage& message)
    {
      bool has_attachments = !message.attachments_.empty();

      // Case 1: Simple plain text, no attachments
      if (!has_attachments)
      {
        GMimePart* text_part = createTextPart(message.text_body);
        g_mime_message_set_mime_part(mime_message, GMIME_OBJECT(text_part));
        g_object_unref(text_part);
        return;
      }

      // Case 2: Has attachments
      // Structure: multipart/mixed -> [body content, attachments...]
      GMimeMultipart* mixed = g_mime_multipart_new_with_subtype("mixed");

      GMimePart* text_part = createTextPart(message.text_body);
      g_mime_multipart_add(mixed, GMIME_OBJECT(text_part));
      g_object_unref(text_part);

      // Add all attachments
      for (const auto& attachment : message.attachments_)
      {
        addAttachment(mixed, attachment);
      }

      g_mime_message_set_mime_part(mime_message, GMIME_OBJECT(mixed));
      g_object_unref(mixed);
    }

  }  // namespace

  namespace writer
  {

    std::string buildMimeMessage(const mail::MailMessage& message, bool hide_bcc)
    {
      // Ensure GMime is initialized via shared MimeContext
      getMimeContext();

      GMimeMessage* mime_message = g_mime_message_new(true);

      // Set all headers
      setFromAddress(mime_message, message.from);
      if (message.reply_to.has_value())
      {
        setReplyTo(mime_message, message.reply_to.value());
      }
      setToAddresses(mime_message, message.email_recipients.to);
      setCcAddresses(mime_message, message.email_recipients.cc);
      setBccAddresses(mime_message, message.email_recipients.bcc);
      setSubject(mime_message, message.subject);
      setDate(mime_message);
      setMessageId(mime_message, message.sender_domain);

      // Set threading headers (RFC 5322)
      if (message.email_threading.in_reply_to.has_value())
      {
        setInReplyTo(mime_message, message.email_threading.in_reply_to.value());
      }
      setReferences(mime_message, message.email_threading.references);

      setMessageBody(mime_message, message);

      // Convert message to string
      GMimeStream* stream = g_mime_stream_mem_new();

      // Create format options - hide BCC header for SMTP sending
      GMimeFormatOptions* options = g_mime_format_options_new();
      g_mime_format_options_set_newline_format(options, GMIME_NEWLINE_FORMAT_DOS);
      if (hide_bcc)
      {
        g_mime_format_options_add_hidden_header(options, "Bcc");
      }

      g_mime_object_write_to_stream(GMIME_OBJECT(mime_message), options, stream);

      GByteArray* byte_array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
      std::string result(reinterpret_cast<const char*>(byte_array->data), byte_array->len);

      g_mime_format_options_free(options);
      g_object_unref(stream);
      g_object_unref(mime_message);

      return result;
    }

  }  // namespace writer
}  // namespace aurora::mail::common::mime
