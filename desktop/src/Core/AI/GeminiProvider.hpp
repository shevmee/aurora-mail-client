#ifndef GEMINI_PROVIDER_HPP
#define GEMINI_PROVIDER_HPP

#include <QByteArray>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief Google Gemini REST API (generateContent) — request/response handling only.
 *
 * Model URL and JSON shape are encapsulated here; AIService owns QNetworkAccessManager and prompts.
 */
class GeminiProvider
{
 public:
  /** Default model path segment (no secrets). */
  static constexpr const char* kDefaultModelPath = "v1beta/models/gemini-3-flash-preview:generateContent";

  /**
   * @brief Build the request URL. The API key is intentionally NOT placed in the URL;
   *        pass it to post() so it is sent via the x-goog-api-key header instead,
   *        keeping it out of logs, proxies, crash reports, and Qt network traces.
   *        The apiKey parameter is kept for API stability but unused.
   */
  [[nodiscard]] static QString buildUrl(const QString& apiKey, const QString& modelPath = QString());

  [[nodiscard]] static QByteArray buildRequestBody(const QString& systemPrompt, const QString& userText);

  /**
   * @brief POST to Gemini; caller must connect to QNetworkReply::finished.
   *        The API key is sent via the x-goog-api-key header (never in the URL).
   */
  [[nodiscard]] static QNetworkReply*
  post(QNetworkAccessManager* nam, const QString& url, const QString& apiKey, const QByteArray& jsonBody);

  /** Parses Gemini generateContent JSON; sets outText or outError. */
  [[nodiscard]] static bool parseResponse(const QByteArray& data, QString& outText, QString& outError);
};

#endif
