#ifndef BASE_CLI_HPP
#define BASE_CLI_HPP

#include <StartupConfig.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace aurora::mail::cli
{

  namespace asio = boost::asio;
  namespace ssl = asio::ssl;

  /**
   * @brief Base class for CLI implementations with shared command infrastructure.
   *
   * Provides common functionality for command registration, execution, help
   * display, and the main run loop. Derived classes implement protocol-specific
   * commands.
   */
  class BaseCli
  {
   protected:
    using CommandHandler = std::function<void(const std::vector<std::string>&)>;

    asio::io_context& io_context;
    ssl::context& ssl_context;
    aurora::mail::common::config::StartupConfig config;
    std::map<std::string, CommandHandler> commands;
    std::map<std::string, std::string> helpTexts;
    bool running = true;

    /**
     * @brief Register a command with its handler and help text.
     */
    void registerCommand(const std::string& name, CommandHandler handler, const std::string& help);

    /**
     * @brief Parse command input handling quoted strings.
     * Splits on whitespace but treats quoted strings as single tokens.
     * Strips surrounding quotes from arguments.
     */
    static std::vector<std::string> parseCommandLine(const std::string& input);

    /**
     * @brief Execute a command from user input.
     */
    void executeCommand(const std::string& input);

    /**
     * @brief Wait for an async operation with timeout.
     */
    void waitForOperation(std::future<void>& fut);

    /**
     * @brief Get the prompt string to display. Override for custom prompts.
     */
    virtual std::string getPrompt() const = 0;

    /**
     * @brief Get the welcome message. Override for protocol-specific messages.
     */
    virtual std::string getWelcomeMessage() const = 0;

    /**
     * @brief Initialize protocol-specific commands. Must be implemented by
     * derived classes.
     */
    virtual void initializeCommands() = 0;

    /**
     * @brief Show help for commands. Override for protocol-specific help.
     */
    virtual void showHelp(const std::vector<std::string>& args) = 0;

    /**
     * @brief Register common built-in commands (help, exit, quit).
     */
    void registerBuiltinCommands();

    /**
     * @brief Show help for a specific command if it exists.
     * @return true if command was found and help was displayed.
     */
    bool showCommandHelp(const std::string& command);

   public:
    BaseCli(asio::io_context& io_ctx, ssl::context& ssl_ctx, const aurora::mail::common::config::StartupConfig& cfg);

    virtual ~BaseCli() = default;

    /**
     * @brief Main run loop - displays prompt and processes commands.
     */
    void run();

    asio::io_context& getIoContext();
    ssl::context& getSslContext();
    const aurora::mail::common::config::StartupConfig& getConfig() const;
    bool isRunning() const;
    void setRunning(bool value);
  };

}  // namespace aurora::mail::cli

#endif  // BASE_CLI_HPP
