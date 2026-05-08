#include <Logger.hpp>
#include <chrono>
#include <format>
#include <iostream>
#include <mutex>

namespace aurora::mail::common::logger
{
  using aurora::mail::common::config::LoggerConfig;

  Logger::Logger(const LoggerConfig& loggerConfig) : config_(loggerConfig), currentSize_(0)
  {
    if (config_.mode == LogMode::File)
    {
      const int64_t timestamp =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      const std::string filePath = std::format(LOG_FILE_FORMAT, timestamp);
      ofs_.open(filePath, std::ios::app);
      if (!ofs_)
      {
        std::cerr << "Failed to open log file: " << filePath << '\n';
      }
      else
      {
        ofs_.seekp(0, std::ios::end);
        currentSize_ = static_cast<std::size_t>(ofs_.tellp());
      }
    }
  }

  Logger::~Logger()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    flushLocked();
  }

  void Logger::log(LogLevel lvl, std::string_view text)
  {
    if (lvl < config_.level)
    {
      return;
    }

    const LogMessage msg(lvl, text, std::chrono::system_clock::now());

    std::lock_guard<std::mutex> lock(mutex_);
    writeLocked(msg);
  }

  void Logger::log(LogLevel lvl, std::string_view text, const std::source_location& loc)
  {
    if (lvl < config_.level)
    {
      return;
    }

    const LogMessage msg(lvl, text, std::chrono::system_clock::now(), loc);

    std::lock_guard<std::mutex> lock(mutex_);
    writeLocked(msg);
  }

  void Logger::flush()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    flushLocked();
  }

  void Logger::writeLocked(const LogMessage& msg)
  {
    if (config_.mode == LogMode::File && ofs_.is_open())
    {
      const auto before = ofs_.tellp();
      ofs_ << msg;
      const auto after = ofs_.tellp();
      if (before != std::ofstream::pos_type(-1) && after != std::ofstream::pos_type(-1))
      {
        currentSize_ += static_cast<std::size_t>(after - before);
      }
    }

    if (config_.mode == LogMode::Stdout)
    {
      std::cout << msg;
    }

    ++messagesSinceFlush_;

    // Flush policy:
    // 1. Error-level messages always flush immediately so they survive crashes.
    // 2. flushIntervalMsgs == 0 means flush every message (legacy behavior).
    // 3. Otherwise flush once we have accumulated enough messages.
    const bool shouldFlush = (msg.level == LogLevel::Error) || (config_.flushIntervalMsgs == 0) ||
                             (messagesSinceFlush_ >= config_.flushIntervalMsgs);

    if (shouldFlush)
    {
      flushLocked();
    }

    if (config_.mode == LogMode::File && currentSize_ > config_.rotateSizeBytes)
    {
      rotateLocked();
    }
  }

  void Logger::flushLocked()
  {
    if (messagesSinceFlush_ == 0)
    {
      return;
    }

    if (config_.mode == LogMode::File && ofs_.is_open())
    {
      ofs_.flush();
    }

    if (config_.mode == LogMode::Stdout)
    {
      std::cout.flush();
    }

    messagesSinceFlush_ = 0;
  }

  void Logger::rotateLocked()
  {
    if (!ofs_)
    {
      return;
    }

    ofs_.close();

    const int64_t timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string newName = std::format(LOG_FILE_FORMAT, timestamp);

    ofs_.open(newName, std::ios::app);
    currentSize_ = 0;
  }

}  // namespace aurora::mail::common::logger
