#include <BaseCli.hpp>
#include <iostream>

namespace aurora::mail::cli
{

  void BaseCli::registerCommand(const std::string& name, CommandHandler handler, const std::string& help)
  {
    commands[name] = std::move(handler);
    helpTexts[name] = help;
  }

  std::vector<std::string> BaseCli::parseCommandLine(const std::string& input)
  {
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';

    for (size_t i = 0; i < input.size(); ++i)
    {
      char c = input[i];

      if (in_quotes)
      {
        if (c == quote_char)
        {
          // End of quoted string - add the accumulated token
          args.push_back(current);
          current.clear();
          in_quotes = false;
          quote_char = '\0';
        }
        else
        {
          current += c;
        }
      }
      else
      {
        if (c == '"' || c == '\'')
        {
          // Start of quoted string
          if (!current.empty())
          {
            // Push what we have so far
            args.push_back(current);
            current.clear();
          }
          in_quotes = true;
          quote_char = c;
        }
        else if (std::isspace(static_cast<unsigned char>(c)))
        {
          if (!current.empty())
          {
            args.push_back(current);
            current.clear();
          }
        }
        else
        {
          current += c;
        }
      }
    }

    // Don't forget the last token
    if (!current.empty())
    {
      args.push_back(current);
    }

    return args;
  }

  void BaseCli::executeCommand(const std::string& input)
  {
    std::vector<std::string> args = parseCommandLine(input);

    if (args.empty())
    {
      return;
    }

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    auto it = commands.find(cmd);
    if (it != commands.end())
    {
      try
      {
        it->second(args);
      }
      catch (const std::exception& e)
      {
        std::cout << "Error executing command: " << e.what() << "\n";
      }
    }
    else
    {
      std::cout << "Unknown command: " << cmd << "\n";
      std::cout << "Type 'help' for available commands\n";
    }
  }

  void BaseCli::waitForOperation(std::future<void>& fut)
  {
    auto status = fut.wait_for(std::chrono::seconds(30));
    if (status == std::future_status::timeout)
    {
      std::cout << "Operation timed out\n";
    }
  }

  void BaseCli::registerBuiltinCommands()
  {
    registerCommand("help", [this](const auto& args) { showHelp(args); }, "help [command] - Show help");
    registerCommand("exit", [this](const auto&) { running = false; }, "exit - Exit CLI");
    registerCommand("quit", [this](const auto&) { running = false; }, "quit - Exit CLI");
  }

  bool BaseCli::showCommandHelp(const std::string& command)
  {
    auto it = helpTexts.find(command);
    if (it != helpTexts.end())
    {
      std::cout << it->second << "\n";
      return true;
    }
    std::cout << "Unknown command: " << command << "\n";
    return false;
  }

  BaseCli::BaseCli(asio::io_context& io_ctx, ssl::context& ssl_ctx, const aurora::mail::common::config::StartupConfig& cfg)
      : io_context(io_ctx),
        ssl_context(ssl_ctx),
        config(cfg)
  {
  }

  void BaseCli::run()
  {
    std::cout << getWelcomeMessage();
    std::cout << "Type 'help' for available commands\n";

    while (running)
    {
      std::cout << getPrompt() << std::flush;
      std::string input;
      std::getline(std::cin, input);

      if (!input.empty())
      {
        executeCommand(input);
      }
    }
  }

  asio::io_context& BaseCli::getIoContext()
  {
    return io_context;
  }
  ssl::context& BaseCli::getSslContext()
  {
    return ssl_context;
  }
  const aurora::mail::common::config::StartupConfig& BaseCli::getConfig() const
  {
    return config;
  }
  bool BaseCli::isRunning() const
  {
    return running;
  }
  void BaseCli::setRunning(bool value)
  {
    running = value;
  }

}  // namespace aurora::mail::cli
