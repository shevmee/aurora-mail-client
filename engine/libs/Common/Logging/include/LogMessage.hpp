#ifndef LOG_MESSAGE_HPP
#define LOG_MESSAGE_HPP

#include <LoggingPrimitives.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <source_location>
#include <string_view>

namespace aurora::mail::common::logger
{
  /// Maximum size for a single log message text (including null terminator)
  inline constexpr std::size_t MAX_LOG_TEXT_SIZE = 1 << 9;

  /// Maximum size for file path in log message
  inline constexpr std::size_t MAX_LOG_FILE_SIZE = 1 << 7;

  /// Maximum size for function name in log message
  inline constexpr std::size_t MAX_LOG_FUNC_SIZE = 1 << 6;

  /**
   * @brief Log message stored in the queue.
   *
   * Uses fixed-size buffers to enable lock-free queue storage without
   * heap allocation. Messages exceeding buffer sizes are truncated.
   */
  struct LogMessage
  {
    static constexpr std::size_t max_text_size = MAX_LOG_TEXT_SIZE;
    static constexpr std::size_t max_file_size = MAX_LOG_FILE_SIZE;
    static constexpr std::size_t max_func_size = MAX_LOG_FUNC_SIZE;

    LogLevel level{};
    std::chrono::system_clock::time_point ts{};
    int line{ 0 };

    std::array<char, max_text_size> text{};
    std::array<char, max_file_size> file{};
    std::array<char, max_func_size> func{};

    template<std::size_t N>
    static void copyTruncated(std::array<char, N>& dst, std::string_view src) noexcept
    {
      static_assert(N > 0, "buffer size must be positive");

      const auto count = std::min(src.size(), N - 1);
      std::ranges::copy_n(src.data(), count, dst.data());
      dst[count] = '\0';

      if (count + 1 < N)
      {
        dst[count + 1] = '\0';  // optional; array is already zero-initialized
      }
    }

    LogMessage() = default;

    /**
     * @brief Construct a log message with explicit source fields.
     *
     * Primarily useful for tests and call sites that want to pass
     * source information without using std::source_location.
     */
    LogMessage(
        LogLevel lvl,
        std::string_view msg_text,
        std::chrono::system_clock::time_point timestamp,
        std::string_view src_file = {},
        int src_line = 0,
        std::string_view src_func = {}) noexcept
        : level(lvl),
          ts(timestamp),
          line(src_line)
    {
      copyTruncated(text, msg_text);
      copyTruncated(file, src_file);
      copyTruncated(func, src_func);
    }

    /**
     * @brief Construct a log message from a std::source_location.
     */
    LogMessage(
        LogLevel lvl,
        std::string_view msg_text,
        std::chrono::system_clock::time_point timestamp,
        const std::source_location& loc) noexcept
        : level(lvl),
          ts(timestamp),
          line(static_cast<int>(loc.line()))
    {
      copyTruncated(text, msg_text);
      copyTruncated(file, std::string_view{ loc.file_name() });
      copyTruncated(func, std::string_view{ loc.function_name() });
    }
  };

  inline std::ostream& operator<<(std::ostream& os, const LogMessage& msg)
  {
    const auto tt = std::chrono::system_clock::to_time_t(msg.ts);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(msg.ts.time_since_epoch()) % 1000;

    std::tm local_tm{};
    localtime_r(&tt, &local_tm);

    os << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count() << " ["
       << to_display_string(msg.level) << "] ";

    if (msg.file[0] != '\0' && msg.line != 0 && msg.func[0] != '\0')
    {
      os << "[" << msg.file.data() << ":" << msg.line << " " << msg.func.data() << "] ";
    }

    os << msg.text.data() << "\n";
    return os;
  }

}  // namespace aurora::mail::common::logger

#endif  // LOG_MESSAGE_HPP
