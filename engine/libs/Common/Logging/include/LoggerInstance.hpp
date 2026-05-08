#ifndef LOGGER_INSTANCE_HPP
#define LOGGER_INSTANCE_HPP

#include <Logger.hpp>
#include <memory>
#include <source_location>
#include <string_view>

namespace aurora::mail::common::logger
{

  class LoggerInstance
  {
   private:
    std::unique_ptr<Logger> logger_;

    LoggerInstance() = default;

   public:
    LoggerInstance(const LoggerInstance&) = delete;
    LoggerInstance(LoggerInstance&) = delete;
    LoggerInstance& operator=(const LoggerInstance&) = delete;
    LoggerInstance& operator=(LoggerInstance&&) = delete;

    static LoggerInstance& instance()
    {
      static LoggerInstance instance;
      return instance;
    }

    void init(const aurora::mail::common::config::LoggerConfig& loggerConfig)
    {
      if (logger_ == nullptr)
      {
        logger_ = std::make_unique<Logger>(loggerConfig);
      }
    }

    Logger& logger()
    {
      return *logger_;
    }
  };

  // Free-function logging API. The std::source_location default argument is
  // evaluated at the *caller's* call site, so file/line/function come from
  // user code -- not from inside this header.
  //
  // In Debug builds (NDEBUG not defined) source location is captured and
  // forwarded. In Release builds (NDEBUG defined) no source location is
  // captured, which avoids leaking source paths into the binary and keeps
  // production logs uncluttered.

#ifndef NDEBUG

  inline void log_debug(std::string_view msg, const std::source_location& loc = std::source_location::current())
  {
    LoggerInstance::instance().logger().log(LogLevel::Debug, msg, loc);
  }

  inline void log_info(std::string_view msg, const std::source_location& loc = std::source_location::current())
  {
    LoggerInstance::instance().logger().log(LogLevel::Info, msg, loc);
  }

  inline void log_warn(std::string_view msg, const std::source_location& loc = std::source_location::current())
  {
    LoggerInstance::instance().logger().log(LogLevel::Warn, msg, loc);
  }

  inline void log_error(std::string_view msg, const std::source_location& loc = std::source_location::current())
  {
    LoggerInstance::instance().logger().log(LogLevel::Error, msg, loc);
  }

#else

  inline void log_debug(std::string_view msg)
  {
    LoggerInstance::instance().logger().log(LogLevel::Debug, msg);
  }

  inline void log_info(std::string_view msg)
  {
    LoggerInstance::instance().logger().log(LogLevel::Info, msg);
  }

  inline void log_warn(std::string_view msg)
  {
    LoggerInstance::instance().logger().log(LogLevel::Warn, msg);
  }

  inline void log_error(std::string_view msg)
  {
    LoggerInstance::instance().logger().log(LogLevel::Error, msg);
  }

#endif

}  // namespace aurora::mail::common::logger

// Expose the logging helpers in the global namespace so existing call sites
// (which previously used macros) can keep calling log_debug/info/warn/error
// without qualification. They remain canonically defined in the
// aurora::mail::common::logger namespace; these are just visibility aliases.
using ::aurora::mail::common::logger::log_debug;
using ::aurora::mail::common::logger::log_error;
using ::aurora::mail::common::logger::log_info;
using ::aurora::mail::common::logger::log_warn;

#endif  // LOGGER_INSTANCE_HPP
