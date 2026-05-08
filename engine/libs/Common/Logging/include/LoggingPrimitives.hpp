#ifndef LOGGING_PRIMITIVES_HPP
#define LOGGING_PRIMITIVES_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace aurora::mail::common::logger
{

  /**
   * @brief Log level used to categorize messages.
   */
  enum class LogLevel : std::uint8_t
  {
    Debug,
    Info,
    Warn,
    Error
  };

  /**
   * @brief Parse log level from string (for config deserialization).
   */
  inline LogLevel parseLogLevel(std::string_view levelStr)
  {
    if (levelStr == "debug")
      return LogLevel::Debug;
    if (levelStr == "info")
      return LogLevel::Info;
    if (levelStr == "warn" || levelStr == "warning")
      return LogLevel::Warn;
    if (levelStr == "error")
      return LogLevel::Error;
    return LogLevel::Info;
  }

  /**
   * @brief Convert log level to lowercase string (for config serialization).
   */
  inline std::string to_string(LogLevel level)
  {
    switch (level)
    {
      case LogLevel::Debug: return "debug";
      case LogLevel::Warn: return "warn";
      case LogLevel::Error: return "error";
      case LogLevel::Info:
      default: return "info";
    }
  }

  /**
   * @brief Convert log level to uppercase string (for log output display).
   */
  inline std::string_view to_display_string(LogLevel level)
  {
    switch (level)
    {
      case LogLevel::Debug: return "DEBUG";
      case LogLevel::Info: return "INFO";
      case LogLevel::Warn: return "WARNING";
      case LogLevel::Error: return "ERROR";
      default: return "UNKNOWN";
    }
  }

  /**
   * @brief Log output destination.
   */
  enum class LogMode : std::uint8_t
  {
    File,
    Stdout
  };

  /**
   * @brief Parse log mode from string (for config deserialization).
   */
  inline LogMode parseLogMode(std::string_view modeStr)
  {
    if (modeStr == "file")
      return LogMode::File;
    if (modeStr == "stdout")
      return LogMode::Stdout;
    return LogMode::File;
  }

  /**
   * @brief Convert log mode to string (for config serialization).
   */
  inline std::string to_string(LogMode mode)
  {
    switch (mode)
    {
      case LogMode::File: return "file";
      case LogMode::Stdout: return "stdout";
      default: return "file";
    }
  }

}  // namespace aurora::mail::common::logger

#endif  // LOGGING_PRIMITIVES_HPP
