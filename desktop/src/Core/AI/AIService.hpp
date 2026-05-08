#ifndef AI_SERVICE_HPP
#define AI_SERVICE_HPP

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <functional>

class GeminiProvider;

namespace aurora::mail::app::ai
{

  /**
   * @brief Supported cloud AI backends (extensible).
   */
  enum class AiProviderKind : int
  {
    Gemini = 0,
    // OpenAI = 1, // reserved
  };

  /**
   * @class AIService
   * @brief AI-assisted compose features via Google Gemini (Google AI Studio API key).
   *
   * API keys are stored with the same secure backend as OAuth (keychain when enabled).
   */
  class AIService : public QObject
  {
    Q_OBJECT

   public:
    /**
     * @brief AI operation result.
     */
    struct Result
    {
      bool success = false;
      QString originalText;
      QString improvedText;
      QString explanation;
      QString errorMessage;
    };

    explicit AIService(QObject* parent = nullptr);
    ~AIService() override;

    Q_DISABLE_COPY_MOVE(AIService)

    [[nodiscard]] bool isConfigured() const noexcept;

    [[nodiscard]] AiProviderKind providerKind() const noexcept
    {
      return m_providerKind;
    }
    void setProviderKind(AiProviderKind kind);

    void setApiKey(const QString& apiKey);
    [[nodiscard]] QString getMaskedApiKey() const;
    void clearApiKey();

    void checkGrammar(const QString& text);
    void improveWriting(const QString& text);
    void makeMoreFormal(const QString& text);
    void makeConcise(const QString& text);

   signals:
    void grammarCheckCompleted(const Result& result);
    void textImprovementCompleted(const Result& result);
    void errorOccurred(const QString& error);

   private:
    void sendRequest(const QString& systemPrompt, const QString& userText, std::function<void(const Result&)> callback);
    void handleResponse(QNetworkReply* reply, const QString& originalText, std::function<void(const Result&)> callback);

    QString loadApiKey() const;
    void saveApiKey(const QString& key);
    void migrateLegacyQSettingsKey();

    QNetworkAccessManager* m_networkManager;
    QString m_apiKey;
    AiProviderKind m_providerKind = AiProviderKind::Gemini;

    static constexpr const char* kLegacySettingsKey = "AI/Google_ApiKey";
  };

}  // namespace aurora::mail::app::ai

#endif  // AI_SERVICE_HPP
