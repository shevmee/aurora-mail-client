#include <ImapCli.hpp>
#include <LoggerInstance.hpp>
#include <MailMessageBuilder.hpp>
#include <SmtpCli.hpp>
#include <StartupConfig.hpp>
#include <boost/asio.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <thread>

namespace asio = boost::asio;
namespace ssl = asio::ssl;

using ConnectionMode = aurora::mail::common::config::ConnectionMode;
using StartupConfig = aurora::mail::common::config::StartupConfig;

constexpr char DEFAULT_CONFIG_FILE[] = "config.json";

// Credentials should be loaded from environment variables or secure config.
// MSVC deprecates std::getenv (C4996) in favour of the bounds-checked
// _dupenv_s; on POSIX std::getenv is the only portable option. Wrap both
// behind a single helper so call sites stay agnostic.
inline std::string getEnvOrEmpty(const char* name)
{
#ifdef _WIN32
  char* raw = nullptr;
  std::size_t len = 0;
  if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr)
  {
    return {};
  }
  std::string value(raw);
  std::free(raw);
  return value;
#else
  const char* val = std::getenv(name);
  return val != nullptr ? val : "";
#endif
}

// JSON deserialization for the CLI's StartupConfig lives here (and only here).
// The engine library proper is JSON-agnostic; the desktop ships its own
// loader in desktop/src/Core/Config/AppConfig.cpp.
namespace
{
ConnectionMode parseConnectionMode(const std::string& s)
{
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "plain") return ConnectionMode::PLAIN;
  if (lower == "starttls") return ConnectionMode::STARTTLS;
  if (lower == "tls" || lower == "ssl" || lower == "smtps" || lower == "imaps" || lower == "ssl_tls")
  {
    return ConnectionMode::SSL_TLS;
  }
  throw std::runtime_error("Invalid connection mode: " + s + ". Valid: plain, starttls, tls");
}

aurora::mail::common::config::LoggerConfig parseLoggerConfig(const nlohmann::json& j)
{
  aurora::mail::common::config::LoggerConfig cfg{};
  cfg.level = aurora::mail::common::logger::parseLogLevel(j.at("level").get<std::string>());
  cfg.mode = aurora::mail::common::logger::parseLogMode(j.at("mode").get<std::string>());
  j.at("queueSize").get_to(cfg.queueSize);
  j.at("rotateSizeBytes").get_to(cfg.rotateSizeBytes);
  cfg.flushIntervalMsgs = j.value("flushIntervalMsgs", static_cast<size_t>(16));
  return cfg;
}

template <typename Cfg>
Cfg parseProtocolConfig(const nlohmann::json& j)
{
  Cfg cfg{};
  j.at("host").get_to(cfg.host);
  cfg.mode = parseConnectionMode(j.at("mode").get<std::string>());
  if (j.contains("port"))
  {
    cfg.port = j.at("port").get<uint16_t>();
  }
  return cfg;
}

std::expected<StartupConfig, std::string> loadStartupConfig(const std::string& filename)
{
  std::ifstream file(filename);
  if (!file.is_open())
  {
    return std::unexpected("Cannot open config file: " + filename);
  }
  nlohmann::json j;
  try
  {
    file >> j;
  }
  catch (const nlohmann::json::parse_error& e)
  {
    return std::unexpected(std::string("Failed to parse JSON config: ") + e.what());
  }
  try
  {
    StartupConfig cfg{};
    j.at("timeout_seconds").get_to(cfg.timeout_seconds);
    cfg.logger = parseLoggerConfig(j.at("logger"));
    cfg.smtp = parseProtocolConfig<aurora::mail::common::config::SmtpConfig>(j.at("smtp"));
    cfg.imap = parseProtocolConfig<aurora::mail::common::config::ImapConfig>(j.at("imap"));
    return cfg;
  }
  catch (const std::exception& e)
  {
    return std::unexpected(std::string("Invalid config structure: ") + e.what());
  }
}
}  // namespace

void setupLogger(const aurora::mail::common::config::StartupConfig& config)
{
  aurora::mail::common::logger::LoggerInstance::instance().init(config.logger);
}

void setupSslContext(ssl::context& ssl_context)
{
  ssl_context.set_verify_mode(ssl::verify_peer);

  boost::system::error_code ec_paths;
  ssl_context.set_default_verify_paths(ec_paths);
  if (ec_paths)
  {
    log_error(std::format("Error setting default verify paths: {}", ec_paths.message()));
  }
  else
  {
    log_info("Default verify paths loaded successfully.");
  }

  ssl_context.set_options(
      ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::no_tlsv1 |
      ssl::context::no_tlsv1_1 | ssl::context::single_dh_use);
  // TLS 1.3 preferred, TLS 1.2 fallback (NFR-04). Anything older is forbidden.
  SSL_CTX_set_min_proto_version(ssl_context.native_handle(), TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ssl_context.native_handle(), TLS1_3_VERSION);
}

void printUsage(const char* program_name)
{
  std::cout << "Usage: " << program_name << " <command> [options]\n\n";
  std::cout << "Commands:\n";
  std::cout << "  imap-cli          Launch interactive IMAP CLI\n";
  std::cout << "  smtp-cli          Launch interactive SMTP CLI\n";
  std::cout << "\nEnvironment Variables:\n";
  std::cout << "  MAIL_USERNAME     Email account username\n";
  std::cout << "  MAIL_PASSWORD     Email account password/app password\n";
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    printUsage(argv[0]);
    return 1;
  }

  std::string command = argv[1];

  StartupConfig config;
  auto config_result = loadStartupConfig(DEFAULT_CONFIG_FILE);
  if (config_result.has_value())
  {
    config = config_result.value();
  }
  else
  {
    // Try fallback paths for config_example.json
    std::vector<std::string> fallback_paths = {
      "config_example.json",     // Project root (when running from root)
      "../config_example.json",  // When running from build/
    };

    bool loaded = false;
    for (const auto& path : fallback_paths)
    {
      auto fallback_result = loadStartupConfig(path);
      if (fallback_result.has_value())
      {
        config = fallback_result.value();
        loaded = true;
        break;
      }
    }

    if (!loaded)
    {
      std::cerr << "Failed to load config. Tried: config.json, "
                   "config_example.json, ../config_example.json\n";
      return 1;
    }
  }

  setupLogger(config);
  log_debug("Configuration loaded successfully");

  try
  {
    asio::io_context io_context;
    // Any TLS version; setupSslContext narrows the negotiation to 1.2/1.3.
    ssl::context ssl_context(ssl::context::tls_client);
    setupSslContext(ssl_context);

    int exit_code = 0;

    if (command == "imap-cli")
    {
      // Interactive IMAP CLI mode
      std::thread worker(
          [&io_context]()
          {
            auto work_guard = asio::make_work_guard(io_context);
            io_context.run();
          });

      try
      {
        aurora::mail::cli::ImapCli cli(io_context, ssl_context, config);
        cli.run();
      }
      catch (const std::exception& e)
      {
        log_error(std::format("CLI error: {}", e.what()));
        exit_code = 1;
      }

      io_context.stop();
      if (worker.joinable())
      {
        worker.join();
      }
    }
    else if (command == "smtp-cli")
    {
      // Interactive SMTP CLI mode
      std::thread worker(
          [&io_context]()
          {
            auto work_guard = asio::make_work_guard(io_context);
            io_context.run();
          });

      try
      {
        aurora::mail::cli::SmtpCli cli(io_context, ssl_context, config);
        cli.run();
      }
      catch (const std::exception& e)
      {
        log_error(std::format("CLI error: {}", e.what()));
        exit_code = 1;
      }

      io_context.stop();
      if (worker.joinable())
      {
        worker.join();
      }
    }
    else
    {
      std::cout << "Unknown command: " << command << "\n\n";
      printUsage(argv[0]);
      exit_code = 1;
    }

    return exit_code;
  }
  catch (const std::exception& e)
  {
    log_error(std::format("FATAL: Exception in main: {}", e.what()));
    return 2;
  }
}
