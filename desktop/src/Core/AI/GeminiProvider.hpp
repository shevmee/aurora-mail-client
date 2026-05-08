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
    static constexpr const char* kDefaultModelPath =
        "v1beta/models/gemini-3-flash-preview:generateContent";

    [[nodiscard]] static QString buildUrl(const QString& apiKey, const QString& modelPath = QString());

    [[nodiscard]] static QByteArray buildRequestBody(const QString& systemPrompt, const QString& userText);

    /**
     * @brief POST to Gemini; caller must connect to QNetworkReply::finished.
     */
    [[nodiscard]] static QNetworkReply* post(QNetworkAccessManager* nam,
                                             const QString& url,
                                             const QByteArray& jsonBody);

    /** Parses Gemini generateContent JSON; sets outText or outError. */
    [[nodiscard]] static bool parseResponse(const QByteArray& data, QString& outText, QString& outError);
};

#endif
