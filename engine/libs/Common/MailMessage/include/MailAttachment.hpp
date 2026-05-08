#ifndef MAIL_ATTACH_HPP
#define MAIL_ATTACH_HPP

#include <filesystem>
#include <string>

namespace aurora::mail::common::mail
{

  /**
   * @brief Represents a file attachment in an email.
   *
   * The `MailAttachment` class encapsulates metadata for an email attachment,
   * such as the file path and the file name. It also defines a static size limit
   * for attachments.
   */
  class MailAttachment
  {
   public:
    /**
     * @brief Constructs a MailAttachment with a given file path.
     *
     * If no path is provided, the attachment is considered empty.
     *
     * @param path The full filesystem path to the attachment file.
     */
    MailAttachment(std::filesystem::path path = "");

    /**
     * @brief Returns the full file path of the attachment.
     *
     * @return The `std::filesystem::path` to the file.
     */
    const std::filesystem::path& getPath() const;

    /**
     * @brief Returns the file name of the attachment.
     *
     * Extracts the file name (including extension) from the full file path.
     *
     * @return The file name as a `std::string`.
     */
    std::string getName() const;

   private:
    std::filesystem::path path_;  ///< Full path to the attachment file.
  };

}  // namespace aurora::mail::common::mail

#endif  // MAIL_ATTACH_HPP
