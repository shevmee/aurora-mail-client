#ifndef MAIL_ADDR_HPP
#define MAIL_ADDR_HPP

#include <expected>
#include <regex>
#include <string>

namespace aurora::mail::common::mail
{
  /**
   * @brief Represents an email address with an optional display name.
   *
   * The `MailAddress` class encapsulates a validated email address and an
   * optional name (e.g., "John Doe <john@example.com>"). It provides methods to
   * retrieve the address and name, and to check the validity of the email format.
   */
  class MailAddress
  {
   public:
    /**
     * @brief Default constructor creating an empty MailAddress.
     */
    MailAddress() = default;

    /**
     * @brief Creates a MailAddress with validation.
     *
     * @param email The email address string (e.g., "user@example.com").
     * @param name The display name associated with the address (e.g., "User
     * Name").
     * @return MailAddress if valid, or error string if invalid.
     */
    static std::expected<MailAddress, std::string> create(std::string email, std::string name = "");

    /**
     * @brief Constructs a MailAddress with an email and optional display name.
     *
     * Note: Does not validate. Use create() for validated construction.
     *
     * @param email The email address string (e.g., "user@example.com").
     * @param name The display name associated with the address (e.g., "User
     * Name").
     */
    MailAddress(std::string email, std::string name = "");

    /**
     * @brief Returns the email address.
     *
     * @return A string view of the email address.
     */
    const std::string& getAddress() const;

    /**
     * @brief Returns the display name.
     *
     * @return A string view of the associated display name.
     */
    const std::string& getName() const;

    /**
     * @brief Checks if the email address is valid.
     *
     * @return true if the email address is valid, false otherwise.
     */
    bool isValid() const;

    bool operator==(const MailAddress& other) const;
    bool operator!=(const MailAddress& other) const;

   private:
    std::string address_;  ///< The email address.
    std::string name_;     ///< The display name associated with the email.

    /**
     * @brief Validates the format of an email address using regular expressions.
     *
     * Performs a basic check to determine if the given email string is valid.
     *
     * @param email The email string to validate.
     * @return true if valid, false otherwise.
     */
    static bool isValidEmail(const std::string& email)
    {
      static const std::regex pattern(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
      return std::regex_match(email, pattern);
    }
  };

}  // namespace aurora::mail::common::mail

#endif  // MAIL_ADDR_HPP
