#ifndef PROTOCOL_ERROR_HPP
#define PROTOCOL_ERROR_HPP

#include <cstdint>
#include <expected>
#include <format>
#include <string>

namespace aurora::mail::common
{

  /**
   * @brief Unified error type for protocol operations.
   *
   * Provides structured error information with context and error codes.
   * Use factory methods for cleaner construction:
   *   ProtocolError::io("Read failed", ec.message())
   *   ProtocolError::protocol("Status mismatch")
   */
  struct ProtocolError
  {
    enum class Category : std::uint8_t
    {
      CONNECTION,      // Connection establishment failed
      IO,              // Read/write operation failed
      PROTOCOL,        // Protocol violation or parsing error
      AUTHENTICATION,  // Authentication failed
      TIMEOUT,         // Operation timed out
      TLS,             // TLS/SSL operation failed
      INVALID_STATE,   // Operation not allowed in current state
      CANCELLED        // Operation was cancelled by user
    };

    Category category;
    std::string message;
    std::string details;  // Optional additional context

    ProtocolError(Category cat, std::string msg, std::string det = "")
        : category(cat),
          message(std::move(msg)),
          details(std::move(det))
    {
    }

    // -- Factory methods for cleaner construction --

    static ProtocolError connection(std::string msg, std::string det = "")
    {
      return { Category::CONNECTION, std::move(msg), std::move(det) };
    }

    static ProtocolError io(std::string msg, std::string det = "")
    {
      return { Category::IO, std::move(msg), std::move(det) };
    }

    static ProtocolError protocol(std::string msg, std::string det = "")
    {
      return { Category::PROTOCOL, std::move(msg), std::move(det) };
    }

    static ProtocolError auth(std::string msg, std::string det = "")
    {
      return { Category::AUTHENTICATION, std::move(msg), std::move(det) };
    }

    static ProtocolError timeout(std::string msg, std::string det = "")
    {
      return { Category::TIMEOUT, std::move(msg), std::move(det) };
    }

    static ProtocolError tls(std::string msg, std::string det = "")
    {
      return { Category::TLS, std::move(msg), std::move(det) };
    }

    static ProtocolError invalidState(std::string msg, std::string det = "")
    {
      return { Category::INVALID_STATE, std::move(msg), std::move(det) };
    }

    static ProtocolError cancelled(std::string msg, std::string det = "")
    {
      return { Category::CANCELLED, std::move(msg), std::move(det) };
    }

    /**
     * @brief Format error as human-readable string.
     */
    std::string toString() const
    {
      std::string result = std::format("[{}] {}", categoryToString(category), message);
      if (!details.empty())
      {
        result += std::format(" ({})", details);
      }
      return result;
    }

    /**
     * @brief Convert category to string for logging/display.
     */
    static const char* categoryToString(Category cat)
    {
      switch (cat)
      {
        case Category::CONNECTION: return "CONNECTION";
        case Category::IO: return "IO";
        case Category::PROTOCOL: return "PROTOCOL";
        case Category::AUTHENTICATION: return "AUTHENTICATION";
        case Category::TIMEOUT: return "TIMEOUT";
        case Category::TLS: return "TLS";
        case Category::INVALID_STATE: return "INVALID_STATE";
        case Category::CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
      }
    }
  };

  /**
   * @brief Result type for operations that can fail.
   *
   * Use this consistently across all protocol operations for uniform error
   * handling.
   */
  template<typename T = void>
  using Result = std::expected<T, ProtocolError>;

  /**
   * @brief Convenience type for operations without return values.
   */
  using VoidResult = Result<void>;

}  // namespace aurora::mail::common

#endif  // PROTOCOL_ERROR_HPP
