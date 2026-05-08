#ifndef ENHANCED_CODE_HPP
#define ENHANCED_CODE_HPP

#include <cctype>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace aurora::mail::smtp::response
{

  /**
   * @brief Represents the category of an SMTP status code.
   */
  enum class ClassCode : std::uint8_t
  {
    Undefined = 0,
    Success = 2,
    PersistentTransientFailure = 4,
    PersistentFailure = 5,
  };

  enum class SubjectCode : std::uint8_t
  {
    Undefined = 0,
    Addressing = 1,
    Mailbox = 2,
    MailSystem = 3,
    Protocol = 5,
    Security = 7,
  };

  /**
   * @brief Get human-readable description of subject code
   */
  inline std::string getSubjectDescription(SubjectCode subject)
  {
    switch (subject)
    {
      case SubjectCode::Undefined: return "Undefined";
      case SubjectCode::Addressing: return "Addressing Status";
      case SubjectCode::Mailbox: return "Mailbox Status";
      case SubjectCode::MailSystem: return "Mail System Status";
      case SubjectCode::Protocol: return "Protocol Status";
      case SubjectCode::Security: return "Security Status";
      default: return "Unknown";
    }
  }

  /**
   * @brief Type-safe representation of SMTP Enhanced Status Code (RFC 3463)
   *
   * Format: X.Y.Z where:
   * - X (class): 2=Success, 4=Transient failure, 5=Permanent failure
   * - Y (subject): Category (0=Undefined, 1=Addressing, 2=Mailbox, 3=Mail system,
   *                4=Network/routing, 5=Mail delivery protocol,
   * 7=Security/policy)
   * - Z (detail): Specific error within the subject category
   *
   * Examples:
   * - 2.0.0 = Success
   * - 5.7.1 = Delivery not authorized, message refused
   * - 4.2.2 = Mailbox full (transient)
   */
  struct EnhancedCode
  {
    ClassCode class_code{ ClassCode::Undefined };   ///< Success/failure class (2/4/5)
    SubjectCode subject{ SubjectCode::Undefined };  ///< Error category (0-7)
    uint8_t detail{ 0 };                            ///< Specific error code

    /**
     * @brief Constructor with values
     */
    EnhancedCode(ClassCode c, SubjectCode s, uint8_t d) : class_code(c), subject(s), detail(d)
    {
    }

    /**
     * @brief Convert to string representation (X.Y.Z)
     */
    std::string toString() const
    {
      return std::format("{}.{}.{}", static_cast<int>(class_code), static_cast<int>(subject), static_cast<int>(detail));
    }

    /**
     * @brief Parse enhanced code from string (X.Y.Z format)
     *
     * @param raw The string to parse (e.g., "5.7.1")
     * @return std::optional<EnhancedCode> Parsed code on success, nullopt on
     * failure
     *
     * @example
     *   auto ec = EnhancedCode::parse("5.7.1");
     *   if (ec) {
     *       std::cout << "Class: " << ec->class_code << "\n"; // 5
     *   }
     *
     * @todo use std::string_view instead of std::string
     */
    static std::optional<EnhancedCode> parse(std::string_view raw)
    {
      if (raw.size() != 5)
      {
        return std::nullopt;
      }

      const char c = raw[0];
      const char dot1 = raw[1];
      const char s = raw[2];
      const char dot2 = raw[3];
      const char d = raw[4];

      if (dot1 != '.' || dot2 != '.')
      {
        return std::nullopt;
      }

      auto isDigit = [](unsigned char c) { return std::isdigit(c) != 0; };

      if (!isDigit(c) || !isDigit(s) || !isDigit(d))
      {
        return std::nullopt;
      }

      const int class_code = c - '0';
      const int subject_code = s - '0';
      const int detail = d - '0';

      // Validate ranges (RFC 3463)
      // Class must be 2, 4, or 5
      if (class_code != 2 && class_code != 4 && class_code != 5)
      {
        return std::nullopt;
      }

      return EnhancedCode{ static_cast<ClassCode>(class_code),
                           static_cast<SubjectCode>(subject_code),
                           static_cast<uint8_t>(detail) };
    }

    /**
     * @brief Check if this is a success code (class 2)
     */
    [[nodiscard]] bool isSuccess() const
    {
      return class_code == ClassCode::Success;
    }

    /**
     * @brief Check if this is a transient failure (class 4)
     */
    [[nodiscard]] bool isTransientFailure() const
    {
      return class_code == ClassCode::PersistentTransientFailure;
    }

    /**
     * @brief Check if this is a permanent failure (class 5)
     */
    [[nodiscard]] bool isPermanentFailure() const
    {
      return class_code == ClassCode::PersistentFailure;
    }

    /**
     * @brief Equality comparison
     */
    bool operator==(const EnhancedCode& other) const = default;

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const EnhancedCode& other) const = default;
  };

}  // namespace aurora::mail::smtp::response

#endif  // ENHANCED_CODE_HPP
