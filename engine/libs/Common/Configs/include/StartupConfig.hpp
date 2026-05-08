#ifndef STARTUP_CONFIG_HPP
#define STARTUP_CONFIG_HPP

#include <LoggingPrimitives.hpp>
#include <cstdint>
#include <optional>
#include <string>

// IMPORTANT: this header is intentionally serialization-agnostic. JSON loading
// of a `StartupConfig` lives in the consuming layer:
//   * desktop:  desktop/src/Core/Config/AppConfig.{hpp,cpp}
//   * CLI tool: engine/main.cpp (inline helper)
// Keeping the engine library free of `nlohmann/json` lets the same engine ship
// without that dependency leaking into every linker line — the CLI binary is
// the only engine artifact that pulls JSON in.

namespace aurora::mail::common::config
{

  using aurora::mail::common::logger::LogLevel;
  using aurora::mail::common::logger::LogMode;

  /**
   * @brief Connection security mode for mail protocols
   */
  enum class ConnectionMode : uint8_t
  {
    PLAIN,     ///< Plain text, no encryption (insecure)
    STARTTLS,  ///< Start plain, upgrade to TLS
    SSL_TLS    ///< Direct TLS connection (implicit TLS) - SMTPS/IMAPS
  };

  /**
   * @brief Logger configuration
   */
  struct LoggerConfig
  {
    LogLevel level;             ///< Log level (deserialized from JSON string)
    LogMode mode;               ///< Log output mode (deserialized from JSON string)
    size_t queueSize;           ///< Log message queue size
    size_t rotateSizeBytes;     ///< Log file rotation size in bytes
    size_t flushIntervalMsgs;   ///< Flush every N messages (0 = flush every message, default: 16)
  };

  /**
   * @brief SMTP server configuration
   */
  struct SmtpConfig
  {
    std::string host;              ///< SMTP server hostname
    ConnectionMode mode;           ///< Connection mode (PLAIN/STARTTLS/TLS)
    std::optional<uint16_t> port;  ///< Custom port (if not set, uses default based on mode)

    /// Get the port to use (custom or default based on mode)
    uint16_t getPort() const;
  };

  /**
   * @brief IMAP server configuration
   */
  struct ImapConfig
  {
    std::string host;              ///< IMAP server hostname
    ConnectionMode mode;           ///< Connection mode (PLAIN/STARTTLS/TLS)
    std::optional<uint16_t> port;  ///< Custom port (if not set, uses default based on mode)

    /// Get the port to use (custom or default based on mode)
    uint16_t getPort() const;
  };

  /**
   * @brief Complete application startup configuration (used by the CLI tool;
   *        the desktop app composes a richer config — see AppConfig).
   */
  struct StartupConfig
  {
    int timeout_seconds;  ///< Network operation timeout
    LoggerConfig logger;  ///< Logger settings
    SmtpConfig smtp;      ///< SMTP server settings
    ImapConfig imap;      ///< IMAP server settings
  };

  /**
   * @brief Get default port for SMTP based on connection mode
   *
   * @param mode Connection mode
   * @return Default port (25 for PLAIN, 587 for STARTTLS, 465 for TLS/SMTPS)
   */
  constexpr uint16_t getSmtpPort(ConnectionMode mode)
  {
    switch (mode)
    {
      case ConnectionMode::PLAIN: return 25;
      case ConnectionMode::SSL_TLS: return 465;  // SMTPS
      case ConnectionMode::STARTTLS: [[fallthrough]];
      default: return 587;  // Default to STARTTLS
    }
  }

  /**
   * @brief Get default port for IMAP based on connection mode
   *
   * @param mode Connection mode
   * @return Default port (143 for PLAIN/STARTTLS, 993 for TLS/IMAPS)
   */
  constexpr uint16_t getImapPort(ConnectionMode mode)
  {
    switch (mode)
    {
      case ConnectionMode::PLAIN: [[fallthrough]];
      case ConnectionMode::STARTTLS: return 143;
      case ConnectionMode::SSL_TLS: [[fallthrough]];
      default: return 993;  // Default to secure
    }
  }

  /**
   * @brief Convert ConnectionMode to string for logging
   */
  inline std::string to_string(ConnectionMode mode)
  {
    switch (mode)
    {
      case ConnectionMode::PLAIN: return "PLAIN";
      case ConnectionMode::STARTTLS: return "STARTTLS";
      case ConnectionMode::SSL_TLS: return "TLS";
    }
    return "UNKNOWN";
  }

}  // namespace aurora::mail::common::config

#endif  // STARTUP_CONFIG_HPP
