#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <LogMessage.hpp>
#include <StartupConfig.hpp>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string_view>

namespace aurora::mail::common::logger
{
  using aurora::mail::common::config::LoggerConfig;

  /// Logging file format string
  inline constexpr std::string_view LOG_FILE_FORMAT = "aurora-mail-{}.log";

  /**
   * @brief Thread-safe synchronous logger.
   *
   * Each `log()` call serializes the message under an internal mutex and writes
   * directly to the configured sink (stdout or a rotating file). Buffered output
   * is flushed according to the configured policy (every Error message, every N
   * messages, or on every message when `flushIntervalMsgs == 0`).
   *
   * Suitable for low-to-moderate log volume. The lock is held only for the
   * duration of formatting and writing one message, so contention between the
   * UI/main thread and the asio io_context thread is negligible at the rates
   * produced by an interactive mail client.
   */
  class Logger
  {
   public:
    /**
     * @brief Constructs a logger from LoggerConfig.
     *
     * All settings (level, mode, rotation size, flush interval) come from
     * the supplied configuration.
     */
    explicit Logger(const LoggerConfig& loggerConfig);

    /**
     * @brief Closes the log file if open.
     */
    ~Logger();

    /**
     * @brief Logs a message without source-location context.
     */
    void log(LogLevel lvl, std::string_view text);

    /**
     * @brief Logs a message with source-location context.
     *
     * Callers that wish to record their own call site should obtain
     * `std::source_location::current()` at that site and forward it (the
     * `log_xxx` helpers in `LoggerInstance.hpp` do this automatically).
     */
    void log(LogLevel lvl, std::string_view text, const std::source_location& loc);

    /**
     * @brief Flushes any buffered output to the underlying sink.
     */
    void flush();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

   private:
    void writeLocked(const LogMessage& msg);  ///< requires mutex_ held
    void flushLocked();                       ///< requires mutex_ held
    void rotateLocked();                      ///< requires mutex_ held

    LoggerConfig config_;
    std::mutex mutex_;
    std::ofstream ofs_;
    std::size_t currentSize_ = 0;
    std::size_t messagesSinceFlush_ = 0;  ///< Counter for periodic flushing
  };

}  // namespace aurora::mail::common::logger

#endif  // LOGGER_HPP
