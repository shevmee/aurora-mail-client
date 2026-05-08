#pragma once

#include <QString>
#include <StartupConfig.hpp>
#include <expected>
#include <string>

namespace aurora::mail::app::config
{

  /**
   * @brief Desktop-side application configuration.
   *
   * Composes only the engine-level fields that the desktop actually uses
   * (timeout, logger) and adds desktop-specific knobs (UI locale).
   *
   * Server hosts/ports are intentionally NOT here: the desktop opens IMAP/SMTP
   * connections per-account from the AccountRegistry, never from a global config.
   *
   * Persisted on disk as JSON at:
   *   QStandardPaths::AppDataLocation/config.json
   *
   * Example file:
   * @code{.json}
   * {
   *   "timeout_seconds": 30,
   *   "locale": "uk",
   *   "logger": {
   *     "level": "info",
   *     "mode": "stdout",
   *     "queueSize": 16384,
   *     "rotateSizeBytes": 104857600
   *   }
   * }
   * @endcode
   */
  struct AppConfig
  {
    /// Network operation timeout (seconds) shared by IMAP and SMTP clients.
    int timeoutSeconds = 30;

    /// Logger settings (engine struct; serialization-agnostic on its definition side).
    aurora::mail::common::config::LoggerConfig logger{};

    /// UI locale tag understood by Qt's QLocale + QTranslator, e.g. "uk", "en".
    /// An empty string means "follow system locale" (QLocale::system()).
    /// Used by main.cpp to install the matching .qm at startup.
    std::string locale;
  };

  /**
   * @brief Load AppConfig from a JSON file on disk.
   *
   * Tolerant: if the JSON only contains the engine-level fields (legacy
   * config.json carried over from the CLI tool), missing desktop fields fall
   * back to defaults — `locale` is left empty so the system locale is used.
   *
   * @param filename Absolute path to the JSON config file.
   * @return Loaded AppConfig on success, or human-readable error string.
   */
  std::expected<AppConfig, std::string> loadAppConfig(const QString& filename);

}  // namespace aurora::mail::app::config
