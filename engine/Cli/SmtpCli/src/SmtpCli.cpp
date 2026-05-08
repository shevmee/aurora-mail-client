#include <SmtpCli.hpp>
#include <iostream>

namespace aurora::mail::cli
{

  SmtpCli::SmtpCli(asio::io_context& io_ctx, ssl::context& ssl_ctx, const aurora::mail::common::config::StartupConfig& cfg)
      : BaseCli(io_ctx, ssl_ctx, cfg)
  {
    smtp_client = std::make_unique<aurora::mail::smtp::SmtpClient>(io_context, ssl_context, config.timeout_seconds);
    initializeCommands();
  }

  std::string SmtpCli::getPrompt() const
  {
    return "smtp> ";
  }

  std::string SmtpCli::getWelcomeMessage() const
  {
    return "=== SMTP CLI Client ===\n";
  }

  void SmtpCli::initializeCommands()
  {
    // Connection commands
    registerCommand(
        "connect", [this](const auto& args) { handleConnect(args); }, "connect [host] [port] - Connect to SMTP server");
    registerCommand("disconnect", [this](const auto& args) { handleDisconnect(args); }, "disconnect - Close connection");

    // Authentication commands
    registerCommand(
        "auth",
        [this](const auto& args) { handleAuth(args); },
        "auth <username> <password> - Authenticate with server (PLAIN)");
    registerCommand(
        "auth-login",
        [this](const auto& args) { handleAuthLogin(args); },
        "auth-login <username> <password> - Authenticate with AUTH LOGIN");

    // Mail commands
    registerCommand(
        "send", [this](const auto& args) { handleSend(args); }, "send [from] [to] [subject] [body] - Send email");
    registerCommand("sendf", [this](const auto& args) { handleSendFile(args); }, "sendf <file> - Send email from file");
    registerCommand("quit", [this](const auto& args) { handleDisconnect(args); }, "quit - Send QUIT command and disconnect");

    // SMTP protocol commands
    registerCommand("noop", [this](const auto& args) { handleNoop(args); }, "noop - Send NOOP command");
    registerCommand("help-smtp", [this](const auto& args) { handleSmtpHelp(args); }, "help-smtp - Get server help");
    registerCommand("vrfy", [this](const auto& args) { handleVrfy(args); }, "vrfy <email> - Verify email address");
    registerCommand("rset", [this](const auto& args) { handleRset(args); }, "rset - Reset current mail transaction");

    // Utility commands
    registerCommand("status", [this](const auto& args) { handleStatus(args); }, "status - Show connection status");
    registerCommand("config", [this](const auto& args) { handleConfig(args); }, "config - Show current configuration");
    registerCommand("caps", [this](const auto&) { handleCapabilities(); }, "caps - Show server capabilities");

    // Built-in commands (from base class)
    registerBuiltinCommands();
  }

  // Connection Management
  void SmtpCli::handleConnect(const std::vector<std::string>& args)
  {
    // Use config defaults if not specified
    std::string host = args.size() > 1 ? args[1] : config.smtp.host;
    uint16_t port = args.size() > 2 ? static_cast<uint16_t>(std::stoul(args[2])) : config.smtp.getPort();

    std::cout << "Connecting to " << host << ":" << port << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, host, port, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncConnect(host, port);
          if (result.has_value())
          {
            std::cout << "Connected successfully\n";
          }
          else
          {
            std::cout << "Connection failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void SmtpCli::handleAuth(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: auth <username> <password>\n";
      return;
    }

    const std::string& username = args[1];
    // Join remaining args as password (handles passwords with spaces)
    std::string password;
    for (size_t i = 2; i < args.size(); ++i)
    {
      if (i > 2)
        password += " ";
      password += args[i];
    }

    std::cout << "Authenticating as " << username << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, username, password, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncAuthenticate(
              aurora::mail::smtp::command::AuthPlain{ .username = username, .password = password });

          if (result.has_value())
          {
            std::cout << "Authentication successful\n";
          }
          else
          {
            std::cout << "Authentication failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void SmtpCli::handleAuthLogin(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: auth-login <username> <password>\n";
      return;
    }

    const std::string& username = args[1];
    // Join remaining args as password (handles passwords with spaces)
    std::string password;
    for (size_t i = 2; i < args.size(); ++i)
    {
      if (i > 2)
        password += " ";
      password += args[i];
    }

    std::cout << "Authenticating with AUTH LOGIN as " << username << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, username, password, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncAuthenticate(
              aurora::mail::smtp::command::AuthLogin{ .username = username, .password = password });

          if (result.has_value())
          {
            std::cout << "Authentication successful (LOGIN)\n";
          }
          else
          {
            std::cout << "Authentication failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  // Email sending - supports both command-line args and interactive mode
  void SmtpCli::handleSend(const std::vector<std::string>& args)
  {
    std::string from, to, subject, body;

    if (args.size() >= 5)
    {
      // Non-interactive mode: send <from> <to> <subject> <body>
      from = args[1];
      to = args[2];
      subject = args[3];
      // Join remaining args as body (in case body has spaces)
      for (size_t i = 4; i < args.size(); ++i)
      {
        if (i > 4)
          body += " ";
        body += args[i];
      }
    }
    else if (args.size() > 1)
    {
      std::cout << "Usage: send <from> <to> <subject> <body>\n";
      std::cout << "   or: send (for interactive mode)\n";
      return;
    }
    else
    {
      // Interactive mode
      std::cout << "=== Interactive Email Composer ===\n";

      std::cout << "From: ";
      std::getline(std::cin, from);

      std::cout << "To: ";
      std::getline(std::cin, to);

      std::cout << "Subject: ";
      std::getline(std::cin, subject);

      std::cout << "Body (end with '.' on new line):\n";
      std::string line;
      while (std::getline(std::cin, line) && line != ".")
      {
        body += line + "\n";
      }
    }

    sendEmail(from, to, subject, body);
  }

  // Send email from file
  void SmtpCli::handleSendFile(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: sendf <filepath>\n";
      std::cout << "File format:\n";
      std::cout << "From: sender@example.com\n";
      std::cout << "To: recipient@example.com\n";
      std::cout << "Subject: Email subject\n";
      std::cout << "\n";
      std::cout << "Email body content...\n";
      return;
    }

    const std::string& filepath = args[1];
    auto email_data = parseEmailFile(filepath);

    if (email_data.has_value())
    {
      auto& [from, to, subject, body] = email_data.value();
      sendEmail(from, to, subject, body);
    }
    else
    {
      std::cout << "Failed to parse email file\n";
    }
  }

  // SMTP Protocol Commands
  void SmtpCli::handleNoop(const std::vector<std::string>&)
  {
    std::cout << "Sending NOOP command...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncNoop();
          if (result.has_value())
          {
            std::cout << "NOOP successful\n";
          }
          else
          {
            std::cout << "NOOP failed\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void SmtpCli::handleVrfy(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: vrfy <email>\n";
      return;
    }

    const std::string& email = args[1];
    std::cout << "Verifying email: " << email << "\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, email, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncVrfy(email);
          if (result.has_value())
          {
            std::cout << "Email verification successful\n";
          }
          else
          {
            std::cout << "Email verification failed\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void
  SmtpCli::sendEmail(const std::string& from, const std::string& to, const std::string& subject, const std::string& body)
  {
    std::cout << "Preparing email...\n";

    aurora::mail::common::mail::MailMessageBuilder builder;
    builder.from(aurora::mail::common::mail::MailAddress(from))
        .to(aurora::mail::common::mail::MailAddress(to))
        .subject(subject)
        .body(body);

    auto message = builder.build();
    if (!message.has_value())
    {
      std::cout << "Failed to build message: " << message.error() << "\n";
      return;
    }

    std::cout << "Sending email...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, msg = std::move(message.value()), &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncSendMail(msg);
          if (result.has_value())
          {
            std::cout << "Mail sent successfully\n";
          }
          else
          {
            std::cout << "Failed to send email: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  std::optional<std::tuple<std::string, std::string, std::string, std::string>> SmtpCli::parseEmailFile(
      const std::string& filepath)
  {
    std::ifstream file(filepath);
    if (!file.is_open())
    {
      std::cout << "Cannot open file: " << filepath << "\n";
      return std::nullopt;
    }

    std::string from, to, subject, body;
    std::string line;
    bool in_body = false;

    while (std::getline(file, line))
    {
      if (!in_body)
      {
        if (line.starts_with("From: "))
        {
          from = line.substr(6);
        }
        else if (line.starts_with("To: "))
        {
          to = line.substr(4);
        }
        else if (line.starts_with("Subject: "))
        {
          subject = line.substr(9);
        }
        else if (line.empty())
        {
          in_body = true;
        }
      }
      else
      {
        body += line + "\n";
      }
    }

    if (from.empty() || to.empty())
    {
      std::cout << "Missing required fields in email file\n";
      return std::nullopt;
    }

    return std::make_tuple(from, to, subject, body);
  }

  void SmtpCli::handleStatus(const std::vector<std::string>&)
  {
    std::cout << "=== SMTP Client Status ===\n";
    std::cout << "Default Server: " << config.smtp.host << ":" << config.smtp.getPort() << "\n";
    std::cout << "Timeout: " << config.timeout_seconds << " seconds\n";
    std::cout << "Connection Mode: " << aurora::mail::common::config::to_string(config.smtp.mode) << "\n";
  }

  void SmtpCli::handleDisconnect(const std::vector<std::string>&)
  {
    std::cout << "Disconnecting...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncQuit();
          if (result.has_value())
          {
            std::cout << "Disconnected successfully\n";
          }
          else
          {
            std::cout << "Disconnect failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void SmtpCli::handleSmtpHelp(const std::vector<std::string>&)
  {
    std::cout << "Requesting server help...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncHelp();
          if (result.has_value())
          {
            std::cout << "Server HELP response received\n";
          }
          else
          {
            std::cout << "HELP failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void SmtpCli::handleRset(const std::vector<std::string>&)
  {
    std::cout << "Resetting mail transaction...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await smtp_client->asyncRset();
          if (result.has_value())
          {
            std::cout << "Mail transaction reset\n";
          }
          else
          {
            std::cout << "RSET failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void SmtpCli::handleConfig(const std::vector<std::string>&)
  {
    std::cout << "=== Current Configuration ===\n";
    std::cout << "SMTP Settings:\n";
    std::cout << "  Host: " << config.smtp.host << "\n";
    std::cout << "  Port: " << config.smtp.getPort() << "\n";
    std::cout << "  Connection Mode: " << aurora::mail::common::config::to_string(config.smtp.mode) << "\n";
    std::cout << "\nGeneral Settings:\n";
    std::cout << "  Timeout: " << config.timeout_seconds << " seconds\n";
    std::cout << "\nIMAP Settings:\n";
    std::cout << "  Host: " << config.imap.host << "\n";
    std::cout << "  Port: " << config.imap.getPort() << "\n";
    std::cout << "  Connection Mode: " << aurora::mail::common::config::to_string(config.imap.mode) << "\n";
  }

  void SmtpCli::handleCapabilities()
  {
    const auto& caps = smtp_client->getCapabilities();

    std::cout << "=== SMTP Server Capabilities ===\n";

    if (caps.extensions.empty())
    {
      std::cout << "No capabilities (not connected or EHLO not sent)\n";
      return;
    }

    // Security
    std::cout << "\nSecurity:\n";
    std::cout << "  STARTTLS: " << (caps.hasStartTls() ? "Yes" : "No") << "\n";

    // Authentication
    std::cout << "\nAuthentication:\n";
    if (caps.hasAuth())
    {
      std::cout << "  Supported mechanisms: ";
      const auto& mechs = caps.getSupportedAuthMechanisms();
      for (size_t i = 0; i < mechs.size(); ++i)
      {
        if (i > 0)
          std::cout << ", ";
        std::cout << mechs[i];
      }
      std::cout << "\n";
      std::cout << "    PLAIN: " << (caps.hasPlainAuth() ? "Yes" : "No") << "\n";
      std::cout << "    LOGIN: " << (caps.hasLoginAuth() ? "Yes" : "No") << "\n";
      std::cout << "    XOAUTH2: " << (caps.hasXOAuth2() ? "Yes" : "No") << "\n";
    }
    else
    {
      std::cout << "  No AUTH extension advertised\n";
    }

    // Message size
    std::cout << "\nMessage Limits:\n";
    size_t max_size = caps.getMaxMessageSize();
    if (max_size > 0)
    {
      std::cout << "  Max message size: " << max_size << " bytes (" << (max_size / 1024 / 1024) << " MB)\n";
    }
    else
    {
      std::cout << "  Max message size: Not specified\n";
    }

    // Features
    std::cout << "\nFeatures:\n";
    std::cout << "  8BITMIME: " << (caps.has8BitMime() ? "Yes" : "No") << "\n";
    std::cout << "  PIPELINING: " << (caps.hasPipelining() ? "Yes" : "No") << "\n";
    std::cout << "  CHUNKING: " << (caps.hasChunking() ? "Yes" : "No") << "\n";
    std::cout << "  SMTPUTF8: " << (caps.hasSmtpUtf8() ? "Yes" : "No") << "\n";
    std::cout << "  DSN: " << (caps.hasDsn() ? "Yes" : "No") << "\n";
    std::cout << "  Enhanced Status: " << (caps.hasEnhancedStatusCodes() ? "Yes" : "No") << "\n";

    // Raw extensions
    std::cout << "\nAll extensions: ";
    bool first = true;
    for (const auto& ext : caps.extensions)
    {
      if (!first)
        std::cout << ", ";
      std::cout << ext;
      first = false;
    }
    std::cout << "\n";
  }

  void SmtpCli::showHelp(const std::vector<std::string>& args)
  {
    if (args.size() > 1)
    {
      showCommandHelp(args[1]);
      return;
    }

    std::cout << "=== SMTP CLI Commands ===\n\n";
    std::cout << "Connection:\n";
    std::cout << "  connect [host] [port]  - Connect to SMTP server\n";
    std::cout << "  auth <user> <pass>     - Authenticate with server\n";
    std::cout << "  disconnect             - Close connection gracefully\n";
    std::cout << "  quit                   - Send QUIT and disconnect\n";
    std::cout << "\nSending Mail:\n";
    std::cout << "  send <from> <to> <subj> <body>\n";
    std::cout << "                         - Send email (or interactive mode)\n";
    std::cout << "  sendf <file>           - Send email from file\n";
    std::cout << "\nSMTP Commands:\n";
    std::cout << "  noop                   - Send NOOP (keep-alive)\n";
    std::cout << "  help-smtp              - Get server HELP\n";
    std::cout << "  vrfy <email>           - Verify email address\n";
    std::cout << "  rset                   - Reset mail transaction\n";
    std::cout << "\nUtility:\n";
    std::cout << "  status                 - Show connection status\n";
    std::cout << "  config                 - Show current configuration\n";
    std::cout << "  caps                   - Show server capabilities\n";
    std::cout << "\n  help / exit\n";
  }

}  // namespace aurora::mail::cli
