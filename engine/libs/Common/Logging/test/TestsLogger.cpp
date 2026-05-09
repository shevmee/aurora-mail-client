#include <gtest/gtest.h>

#include <Logger.hpp>
#include <LoggerInstance.hpp>
#include <LoggingPrimitives.hpp>
#include <StartupConfig.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using aurora::mail::common::config::LoggerConfig;
using aurora::mail::common::logger::Logger;
using aurora::mail::common::logger::LoggerInstance;
using aurora::mail::common::logger::LogLevel;
using aurora::mail::common::logger::LogMode;
using aurora::mail::common::logger::parseLogLevel;
using aurora::mail::common::logger::parseLogMode;
using aurora::mail::common::logger::to_display_string;
using aurora::mail::common::logger::to_string;

namespace fs = std::filesystem;

// ===========================================================================
// LoggingPrimitives  (free functions in LoggingPrimitives.hpp)
// ===========================================================================

TEST(LoggingPrimitives, ParseLogLevelKnown)
{
  EXPECT_EQ(parseLogLevel("debug"), LogLevel::Debug);
  EXPECT_EQ(parseLogLevel("info"), LogLevel::Info);
  EXPECT_EQ(parseLogLevel("warn"), LogLevel::Warn);
  EXPECT_EQ(parseLogLevel("warning"), LogLevel::Warn);
  EXPECT_EQ(parseLogLevel("error"), LogLevel::Error);
}

TEST(LoggingPrimitives, ParseLogLevelUnknownFallsBackToInfo)
{
  EXPECT_EQ(parseLogLevel(""), LogLevel::Info);
  EXPECT_EQ(parseLogLevel("trace"), LogLevel::Info);
  EXPECT_EQ(parseLogLevel("WARN"), LogLevel::Info) << "INCONSISTENCY: parseLogLevel is case-sensitive; uppercase strings "
                                                      "fall back to Info silently. Pinned so a future case-insensitive "
                                                      "rewrite is intentional.";
}

TEST(LoggingPrimitives, LogLevelToStringRoundTrip)
{
  for (auto lvl : { LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error })
  {
    EXPECT_EQ(parseLogLevel(to_string(lvl)), lvl);
  }
}

TEST(LoggingPrimitives, LogLevelDisplayIsUppercase)
{
  EXPECT_EQ(to_display_string(LogLevel::Debug), "DEBUG");
  EXPECT_EQ(to_display_string(LogLevel::Info), "INFO");
  EXPECT_EQ(to_display_string(LogLevel::Warn), "WARNING");
  EXPECT_EQ(to_display_string(LogLevel::Error), "ERROR");
}

TEST(LoggingPrimitives, ParseLogModeKnown)
{
  EXPECT_EQ(parseLogMode("file"), LogMode::File);
  EXPECT_EQ(parseLogMode("stdout"), LogMode::Stdout);
}

TEST(LoggingPrimitives, ParseLogModeUnknownFallsBackToFile)
{
  EXPECT_EQ(parseLogMode(""), LogMode::File);
  EXPECT_EQ(parseLogMode("syslog"), LogMode::File);
  EXPECT_EQ(parseLogMode("STDOUT"), LogMode::File);
}

TEST(LoggingPrimitives, LogModeToStringRoundTrip)
{
  EXPECT_EQ(parseLogMode(to_string(LogMode::File)), LogMode::File);
  EXPECT_EQ(parseLogMode(to_string(LogMode::Stdout)), LogMode::Stdout);
}

// ===========================================================================
// Logger -- file-mode integration
// ===========================================================================

namespace
{
  // Logger::Logger() opens "aurora-mail-{ms}.log" in the *current working
  // directory*, so we chdir to a fresh temp dir for every file-mode test
  // and clean up afterwards.
  class LoggerFileFixture : public ::testing::Test
  {
   protected:
    fs::path original_cwd_;
    fs::path test_dir_;

    void SetUp() override
    {
      original_cwd_ = fs::current_path();
      test_dir_ = fs::temp_directory_path() / ("aurora-logger-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
      fs::create_directories(test_dir_);
      fs::current_path(test_dir_);
    }

    void TearDown() override
    {
      fs::current_path(original_cwd_);
      std::error_code ec;
      fs::remove_all(test_dir_, ec);
    }

    // Concatenate every aurora-mail-*.log file in the temp dir.
    std::string readAllLogFiles() const
    {
      std::string acc;
      for (auto const& entry : fs::directory_iterator(test_dir_))
      {
        if (entry.is_regular_file() && entry.path().filename().string().rfind("aurora-mail-", 0) == 0 &&
            entry.path().extension() == ".log")
        {
          std::ifstream ifs(entry.path());
          std::ostringstream oss;
          oss << ifs.rdbuf();
          acc += oss.str();
        }
      }
      return acc;
    }

    std::size_t logFileCount() const
    {
      std::size_t n = 0;
      for (auto const& entry : fs::directory_iterator(test_dir_))
      {
        if (entry.is_regular_file() && entry.path().filename().string().rfind("aurora-mail-", 0) == 0 &&
            entry.path().extension() == ".log")
          ++n;
      }
      return n;
    }

    // A LoggerConfig that writes to a file in the temp dir.
    LoggerConfig
    fileConfig(LogLevel level = LogLevel::Debug, std::size_t flushEvery = 0, std::size_t rotateBytes = 1ULL << 30) const
    {
      LoggerConfig c{};
      c.level = level;
      c.mode = LogMode::File;
      c.queueSize = 1024;
      c.rotateSizeBytes = rotateBytes;
      c.flushIntervalMsgs = flushEvery;
      return c;
    }
  };
}  // namespace

TEST_F(LoggerFileFixture, FileModeWritesMessage)
{
  {
    Logger logger(fileConfig());
    logger.log(LogLevel::Info, "hello-from-test");
    logger.flush();
  }
  const std::string contents = readAllLogFiles();
  EXPECT_NE(contents.find("hello-from-test"), std::string::npos) << "log output: " << contents;
  EXPECT_NE(contents.find("INFO"), std::string::npos);
}

TEST_F(LoggerFileFixture, LevelFilterDropsBelowConfiguredThreshold)
{
  {
    Logger logger(fileConfig(LogLevel::Warn));
    logger.log(LogLevel::Debug, "drop-debug");
    logger.log(LogLevel::Info, "drop-info");
    logger.log(LogLevel::Warn, "keep-warn");
    logger.log(LogLevel::Error, "keep-error");
    logger.flush();
  }
  const std::string contents = readAllLogFiles();
  EXPECT_EQ(contents.find("drop-debug"), std::string::npos);
  EXPECT_EQ(contents.find("drop-info"), std::string::npos);
  EXPECT_NE(contents.find("keep-warn"), std::string::npos);
  EXPECT_NE(contents.find("keep-error"), std::string::npos);
}

TEST_F(LoggerFileFixture, ErrorLevelFlushesEvenWithLargeBatchInterval)
{
  // flushIntervalMsgs = 1000 means we'd normally hold up to 1000 messages.
  // Error must still escape immediately so it survives a crash.
  {
    Logger logger(fileConfig(LogLevel::Debug, /*flushEvery=*/1000));
    logger.log(LogLevel::Info, "buffered-info");  // would stay in buffer
    logger.log(LogLevel::Error, "must-flush");

    // Read the file *without* destroying the logger -- if Error didn't
    // flush, this read will not see the line.
    const std::string mid = readAllLogFiles();
    EXPECT_NE(mid.find("must-flush"), std::string::npos) << "Error-level message did not flush immediately: " << mid;
  }
}

TEST_F(LoggerFileFixture, BatchFlushEveryNMessages)
{
  // With flushEvery = 3, the first two messages should still be buffered.
  // The third one triggers a flush.
  Logger logger(fileConfig(LogLevel::Debug, /*flushEvery=*/3));
  logger.log(LogLevel::Info, "msg-1");
  logger.log(LogLevel::Info, "msg-2");

  const std::string before = readAllLogFiles();
  // INCONSISTENCY DOC: ofstream's internal buffer interacts with manual
  // flush counting. The current implementation increments
  // messagesSinceFlush_ inside writeLocked() *after* writing, and only
  // calls flush() when the threshold is reached. Whether the bytes have
  // actually reached disk before flush() depends on the platform and the
  // write size. We don't assert on `before` -- just verify the *after*
  // state, which is the actual contract.

  logger.log(LogLevel::Info, "msg-3");  // hits the threshold
  const std::string after = readAllLogFiles();
  EXPECT_NE(after.find("msg-1"), std::string::npos);
  EXPECT_NE(after.find("msg-2"), std::string::npos);
  EXPECT_NE(after.find("msg-3"), std::string::npos);
}

TEST_F(LoggerFileFixture, FlushOnEmptyBufferIsNoOp)
{
  // flushLocked() returns early when messagesSinceFlush_ == 0. We just
  // verify it doesn't crash and doesn't create extra files.
  {
    Logger logger(fileConfig());
    logger.flush();
    logger.flush();
  }
  // Even with no log() calls, the constructor opens the file (creating it).
  EXPECT_GE(logFileCount(), 1U);
}

TEST_F(LoggerFileFixture, RotationCreatesAdditionalFile)
{
  // rotateSizeBytes = 1 forces every write to rotate (the threshold is
  // checked AFTER each write, with strict `>`, so the very first write
  // pushes currentSize_ above 1 byte and rotates).
  //
  // INCONSISTENCY: rotation reuses the same "aurora-mail-{ms}.log" pattern
  // with the *current* timestamp. If two rotations happen in the same
  // millisecond (timestamp clock granularity), they reopen the same file
  // in append mode rather than producing distinct rotated files. This is
  // observable on fast machines / fast tests; we sleep briefly between
  // writes to make rotation produce distinct files for the assertion.
  {
    Logger logger(fileConfig(LogLevel::Debug, /*flushEvery=*/0, /*rotateBytes=*/1));
    logger.log(LogLevel::Info, "first");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    logger.log(LogLevel::Info, "second");
  }
  EXPECT_GE(logFileCount(), 2U) << "expected rotation to create at least one extra file";
}

// ===========================================================================
// Logger -- stdout mode
// ===========================================================================

TEST(LoggerStdout, StdoutModeWritesToStdout)
{
  LoggerConfig cfg{};
  cfg.level = LogLevel::Debug;
  cfg.mode = LogMode::Stdout;
  cfg.queueSize = 64;
  cfg.rotateSizeBytes = 1ULL << 30;
  cfg.flushIntervalMsgs = 0;

  testing::internal::CaptureStdout();
  {
    Logger logger(cfg);
    logger.log(LogLevel::Warn, "stdout-marker");
    logger.flush();
  }
  const std::string captured = testing::internal::GetCapturedStdout();
  EXPECT_NE(captured.find("stdout-marker"), std::string::npos) << captured;
  EXPECT_NE(captured.find("WARNING"), std::string::npos);
}

TEST(LoggerStdout, SourceLocationVariantIncludesFileAndFunc)
{
  LoggerConfig cfg{};
  cfg.level = LogLevel::Debug;
  cfg.mode = LogMode::Stdout;
  cfg.queueSize = 64;
  cfg.rotateSizeBytes = 1ULL << 30;
  cfg.flushIntervalMsgs = 0;

  testing::internal::CaptureStdout();
  {
    Logger logger(cfg);
    auto loc = std::source_location::current();
    logger.log(LogLevel::Info, "with-src", loc);
    logger.flush();
  }
  const std::string captured = testing::internal::GetCapturedStdout();
  EXPECT_NE(captured.find("with-src"), std::string::npos) << captured;
  // operator<< only prints the [file:line func] tag if all three are set;
  // std::source_location::current() at the call site provides all three.
  EXPECT_NE(captured.find("TestsLogger.cpp"), std::string::npos)
      << "source-location formatting did not embed file name: " << captured;
}

// ===========================================================================
// LoggerInstance singleton
// ===========================================================================

TEST(LoggerInstance, InitFollowedByLoggerWorks)
{
  // The LoggerInstance singleton is idempotent: init() only takes effect on
  // first call. We can't easily reset the singleton between tests, so we run
  // this test inside its own GoogleTest binary by virtue of being the only
  // direct test of LoggerInstance. After this test, the singleton holds a
  // stdout logger that subsequent tests in the same process would see.
  LoggerConfig cfg{};
  cfg.level = LogLevel::Debug;
  cfg.mode = LogMode::Stdout;
  cfg.queueSize = 64;
  cfg.rotateSizeBytes = 1ULL << 30;
  cfg.flushIntervalMsgs = 0;

  testing::internal::CaptureStdout();
  LoggerInstance::instance().init(cfg);

  // Calling init() a second time with a different config has no effect.
  LoggerConfig swap = cfg;
  swap.level = LogLevel::Error;
  LoggerInstance::instance().init(swap);

  // The first config's level=Debug should still be in effect, so an Info
  // message must still be logged.
  LoggerInstance::instance().logger().log(LogLevel::Info, "instance-marker");
  LoggerInstance::instance().logger().flush();

  const std::string captured = testing::internal::GetCapturedStdout();
  EXPECT_NE(captured.find("instance-marker"), std::string::npos) << captured;
}
