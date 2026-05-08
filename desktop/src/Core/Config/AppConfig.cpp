#include "AppConfig.hpp"

#include <LoggingPrimitives.hpp>

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <cstddef>

namespace aurora::mail::app::config
{

namespace
{

std::expected<aurora::mail::common::config::LoggerConfig, std::string>
parseLoggerConfig(const QJsonObject& obj)
{
    namespace logger_ns = aurora::mail::common::logger;
    aurora::mail::common::config::LoggerConfig cfg{};

    const auto level = obj.value(QStringLiteral("level"));
    const auto mode = obj.value(QStringLiteral("mode"));
    const auto queueSize = obj.value(QStringLiteral("queueSize"));
    const auto rotateSize = obj.value(QStringLiteral("rotateSizeBytes"));
    if (!level.isString() || !mode.isString() || !queueSize.isDouble() || !rotateSize.isDouble())
    {
        return std::unexpected(std::string(
            "logger: required fields missing or wrong type "
            "(level: string, mode: string, queueSize: number, rotateSizeBytes: number)"));
    }

    cfg.level = logger_ns::parseLogLevel(level.toString().toStdString());
    cfg.mode = logger_ns::parseLogMode(mode.toString().toStdString());
    cfg.queueSize = static_cast<std::size_t>(queueSize.toDouble());
    cfg.rotateSizeBytes = static_cast<std::size_t>(rotateSize.toDouble());

    const auto flush = obj.value(QStringLiteral("flushIntervalMsgs"));
    cfg.flushIntervalMsgs =
        flush.isDouble() ? static_cast<std::size_t>(flush.toDouble()) : static_cast<std::size_t>(16);
    return cfg;
}

}  // namespace

std::expected<AppConfig, std::string> loadAppConfig(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return std::unexpected("Cannot open config file: " + filename.toStdString());
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        return std::unexpected(
            std::string("Failed to parse JSON config: ") + parseError.errorString().toStdString());
    }
    if (!doc.isObject())
    {
        return std::unexpected(std::string("Config root must be a JSON object"));
    }

    const QJsonObject root = doc.object();
    AppConfig cfg{};

    const auto timeoutValue = root.value(QStringLiteral("timeout_seconds"));
    if (!timeoutValue.isDouble())
    {
        return std::unexpected(std::string("Missing or non-numeric \"timeout_seconds\""));
    }
    cfg.timeoutSeconds = static_cast<int>(timeoutValue.toDouble());

    const auto loggerValue = root.value(QStringLiteral("logger"));
    if (!loggerValue.isObject())
    {
        return std::unexpected(std::string("Missing or invalid \"logger\" object"));
    }
    auto loggerCfg = parseLoggerConfig(loggerValue.toObject());
    if (!loggerCfg)
    {
        return std::unexpected(loggerCfg.error());
    }
    cfg.logger = *loggerCfg;

    // locale is optional. Keys we accept (in order of precedence):
    //   "locale": "uk"  — explicit BCP-47-ish locale tag
    //   "ui": { "locale": "uk" } — namespaced form, for forward compatibility
    // An absent value means "use system locale" (handled by main.cpp).
    if (const auto locale = root.value(QStringLiteral("locale")); locale.isString())
    {
        cfg.locale = locale.toString().toStdString();
    }
    else if (const auto ui = root.value(QStringLiteral("ui")); ui.isObject())
    {
        const auto uiLocale = ui.toObject().value(QStringLiteral("locale"));
        if (uiLocale.isString())
        {
            cfg.locale = uiLocale.toString().toStdString();
        }
    }

    return cfg;
}

}  // namespace aurora::mail::app::config
