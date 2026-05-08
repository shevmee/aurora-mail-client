#ifndef SMTP_CLI_HPP
#define SMTP_CLI_HPP

#include <BaseCli.hpp>
#include <MailMessageBuilder.hpp>
#include <SmtpClient.hpp>
#include <SmtpCommand.hpp>
#include <memory>
#include <optional>
#include <tuple>

namespace aurora::mail::cli
{

  class SmtpCli : public BaseCli
  {
    std::unique_ptr<aurora::mail::smtp::SmtpClient> smtp_client;

   public:
    SmtpCli(asio::io_context& io_ctx, ssl::context& ssl_ctx, const aurora::mail::common::config::StartupConfig& cfg);

   protected:
    std::string getPrompt() const override;
    std::string getWelcomeMessage() const override;
    void initializeCommands() override;
    void showHelp(const std::vector<std::string>& args) override;

    void handleConnect(const std::vector<std::string>& args);
    void handleAuth(const std::vector<std::string>& args);
    void handleAuthLogin(const std::vector<std::string>& args);
    void handleSend(const std::vector<std::string>& args);
    void handleSendFile(const std::vector<std::string>& args);
    void handleNoop(const std::vector<std::string>& args);
    void handleVrfy(const std::vector<std::string>& args);
    void handleRset(const std::vector<std::string>& args);
    void handleStatus(const std::vector<std::string>& args);
    void handleDisconnect(const std::vector<std::string>& args);
    void handleSmtpHelp(const std::vector<std::string>& args);
    void handleConfig(const std::vector<std::string>& args);
    void handleCapabilities();

    void sendEmail(const std::string& from, const std::string& to, const std::string& subject, const std::string& body);

    std::optional<std::tuple<std::string, std::string, std::string, std::string>> parseEmailFile(
        const std::string& filepath);
  };

}  // namespace aurora::mail::cli

#endif  // SMTP_CLI_HPP
