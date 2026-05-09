#include <gtest/gtest.h>

#include <Config/AppConfig.hpp>
#include <LoggingPrimitives.hpp>
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryFile>
#include <StartupConfig.hpp>

#include "QtTestSupport.hpp"

using aurora::mail::app::config::AppConfig;
using aurora::mail::app::config::loadAppConfig;
using aurora::mail::common::logger::LogLevel;
using aurora::mail::common::logger::LogMode;

namespace
{
  // Persist `json` to a temp .json file and return its path. The caller
  // owns the temp file via the returned QTemporaryFile (kept alive by
  // unique_ptr to allow auto-cleanup at end of test).
  std::unique_ptr<QTemporaryFile> writeTempJson(const QByteArray& json)
  {
    auto tmp = std::make_unique<QTemporaryFile>();
    tmp->setAutoRemove(true);
    if (!tmp->open())
      return nullptr;
    tmp->write(json);
    tmp->close();
    return tmp;
  }
}  // namespace

// ---------------------------------------------------------------------------
// Happy paths
// ---------------------------------------------------------------------------

TEST(AppConfig, LoadsCompleteConfig)
{
  const QByteArray json = R"({
    "timeout_seconds": 45,
    "locale": "uk",
    "logger": {
      "level": "warn",
      "mode": "stdout",
      "queueSize": 16384,
      "rotateSizeBytes": 104857600,
      "flushIntervalMsgs": 32
    }
  })";
  auto tmp = writeTempJson(json);
  ASSERT_NE(tmp, nullptr);

  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value()) << cfg.error();
  EXPECT_EQ(cfg->timeoutSeconds, 45);
  EXPECT_EQ(cfg->locale, "uk");
  EXPECT_EQ(cfg->logger.level, LogLevel::Warn);
  EXPECT_EQ(cfg->logger.mode, LogMode::Stdout);
  EXPECT_EQ(cfg->logger.queueSize, 16384U);
  EXPECT_EQ(cfg->logger.rotateSizeBytes, 104857600U);
  EXPECT_EQ(cfg->logger.flushIntervalMsgs, 32U);
}

TEST(AppConfig, LoaderToleratesMissingFlushInterval)
{
  // flushIntervalMsgs is documented as optional. The default is 16.
  const QByteArray json = R"({
    "timeout_seconds": 30,
    "logger": {
      "level": "info",
      "mode": "file",
      "queueSize": 1024,
      "rotateSizeBytes": 1048576
    }
  })";
  auto tmp = writeTempJson(json);
  ASSERT_NE(tmp, nullptr);

  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value()) << cfg.error();
  EXPECT_EQ(cfg->logger.flushIntervalMsgs, 16U) << "flushIntervalMsgs must default to 16 when absent.";
}

TEST(AppConfig, LoaderToleratesMissingLocale)
{
  // locale is optional; absence => empty string => "follow system locale".
  const QByteArray json = R"({
    "timeout_seconds": 10,
    "logger": {
      "level": "info",
      "mode": "stdout",
      "queueSize": 64,
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value());
  EXPECT_TRUE(cfg->locale.empty());
}

TEST(AppConfig, NamespacedUiLocaleIsAccepted)
{
  // Forward-compat shape: { "ui": { "locale": "uk" } }
  const QByteArray json = R"({
    "timeout_seconds": 30,
    "ui": { "locale": "uk" },
    "logger": {
      "level": "info",
      "mode": "stdout",
      "queueSize": 64,
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value());
  EXPECT_EQ(cfg->locale, "uk");
}

TEST(AppConfig, TopLevelLocaleWinsOverNamespacedUi)
{
  // Both forms present: top-level `locale` is documented as primary.
  const QByteArray json = R"({
    "timeout_seconds": 30,
    "locale": "en",
    "ui": { "locale": "uk" },
    "logger": {
      "level": "info",
      "mode": "stdout",
      "queueSize": 64,
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value());
  EXPECT_EQ(cfg->locale, "en");
}

TEST(AppConfig, UnknownLogLevelFallsBackSilently)
{
  // INCONSISTENCY DOC: parseLogLevel returns LogLevel::Info on unknown
  // input rather than failing the load. Pinned so a future strict mode is
  // intentional.
  const QByteArray json = R"({
    "timeout_seconds": 30,
    "logger": {
      "level": "ludicrous",
      "mode": "stdout",
      "queueSize": 64,
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value()) << cfg.error();
  EXPECT_EQ(cfg->logger.level, LogLevel::Info);
}

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

TEST(AppConfig, MissingFileReturnsError)
{
  auto cfg = loadAppConfig(QStringLiteral("/this/path/should/never/exist/aurora.json"));
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("Cannot open config file"), std::string::npos) << cfg.error();
}

TEST(AppConfig, InvalidJsonReturnsError)
{
  auto tmp = writeTempJson(QByteArray("{ this is not valid json"));
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("Failed to parse JSON"), std::string::npos) << cfg.error();
}

TEST(AppConfig, RootMustBeObject)
{
  auto tmp = writeTempJson(QByteArray("[1, 2, 3]"));
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("must be a JSON object"), std::string::npos) << cfg.error();
}

TEST(AppConfig, MissingTimeoutSecondsRejected)
{
  const QByteArray json = R"({
    "logger": {
      "level": "info",
      "mode": "stdout",
      "queueSize": 1,
      "rotateSizeBytes": 1
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("timeout_seconds"), std::string::npos);
}

TEST(AppConfig, NonNumericTimeoutRejected)
{
  const QByteArray json = R"({
    "timeout_seconds": "thirty",
    "logger": {
      "level": "info",
      "mode": "stdout",
      "queueSize": 1,
      "rotateSizeBytes": 1
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("timeout_seconds"), std::string::npos);
}

TEST(AppConfig, MissingLoggerObjectRejected)
{
  const QByteArray json = R"({ "timeout_seconds": 30 })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("logger"), std::string::npos);
}

TEST(AppConfig, LoggerWithMissingFieldRejected)
{
  // queueSize missing.
  const QByteArray json = R"({
    "timeout_seconds": 30,
    "logger": {
      "level": "info",
      "mode": "stdout",
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
  EXPECT_NE(cfg.error().find("logger"), std::string::npos);
}

TEST(AppConfig, LoggerWithWrongFieldTypeRejected)
{
  // level given as a number.
  const QByteArray json = R"({
    "timeout_seconds": 30,
    "logger": {
      "level": 5,
      "mode": "stdout",
      "queueSize": 64,
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_FALSE(cfg.has_value());
}

// ---------------------------------------------------------------------------
// Behaviour quirks worth pinning
// ---------------------------------------------------------------------------

TEST(AppConfig, FloatTimeoutIsTruncatedToInt)
{
  // QJsonValue::toDouble() returns 30.7; static_cast<int> drops to 30.
  // Pin so a future change to round-half-up is intentional.
  const QByteArray json = R"({
    "timeout_seconds": 30.7,
    "logger": {
      "level": "info",
      "mode": "stdout",
      "queueSize": 64,
      "rotateSizeBytes": 1024
    }
  })";
  auto tmp = writeTempJson(json);
  auto cfg = loadAppConfig(tmp->fileName());
  ASSERT_TRUE(cfg.has_value());
  EXPECT_EQ(cfg->timeoutSeconds, 30);
}
