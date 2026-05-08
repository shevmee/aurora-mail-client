#include "AIService.hpp"
#include "AiApiKeyStorage.hpp"
#include "GeminiProvider.hpp"

#include <QSettings>
#include <QDebug>

namespace aurora::mail::app::ai {

AIService::AIService(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    QSettings settings;
    m_providerKind = static_cast<AiProviderKind>(
        settings.value(QStringLiteral("AI/ProviderKind"), static_cast<int>(AiProviderKind::Gemini)).toInt());

    m_apiKey = loadApiKey();
    migrateLegacyQSettingsKey();
}

AIService::~AIService() = default;

bool AIService::isConfigured() const noexcept
{
    return !m_apiKey.isEmpty();
}

void AIService::setProviderKind(AiProviderKind kind)
{
    m_providerKind = kind;
    QSettings settings;
    settings.setValue(QStringLiteral("AI/ProviderKind"), static_cast<int>(kind));
}

void AIService::setApiKey(const QString& apiKey)
{
    m_apiKey = apiKey;
    saveApiKey(apiKey);
}

QString AIService::getMaskedApiKey() const
{
    if (m_apiKey.isEmpty()) {
        return QString();
    }
    if (m_apiKey.length() <= 8) {
        return QStringLiteral("****");
    }
    return m_apiKey.left(4) + QLatin1String("...") + m_apiKey.right(4);
}

void AIService::clearApiKey()
{
    m_apiKey.clear();
    saveApiKey(QString());
}

QString AIService::loadApiKey() const
{
    AiApiKeyStorage storage;
    QString key = storage.load();
    if (!key.isEmpty()) {
        qDebug() << "[AI] API key loaded from secure storage";
    }
    return key;
}

void AIService::saveApiKey(const QString& key)
{
    AiApiKeyStorage storage;
    if (key.isEmpty()) {
        storage.clear();
        qDebug() << "[AI] API key cleared";
    } else {
        storage.save(key);
        qDebug() << "[AI] API key saved to secure storage";
    }
}

void AIService::migrateLegacyQSettingsKey()
{
    AiApiKeyStorage storage;
    if (!storage.load().isEmpty()) {
        return;
    }

    QSettings settings;
    const QString legacy = settings.value(QString::fromLatin1(kLegacySettingsKey)).toString();
    if (legacy.isEmpty()) {
        return;
    }

    storage.save(legacy);
    settings.remove(QString::fromLatin1(kLegacySettingsKey));
    m_apiKey = legacy;
    qDebug() << "[AI] Migrated API key from QSettings to secure storage";
}

void AIService::checkGrammar(const QString& text)
{
    const QString systemPrompt = QStringLiteral(
        R"(You are a grammar and spelling checker. 
Analyze the given text and provide a corrected version.
- Fix any grammar, spelling, or punctuation errors
- Preserve the original meaning and tone
- Keep the same language as the input
- If the text is already correct, return it unchanged

Respond with ONLY the corrected text, no explanations.)");

    sendRequest(systemPrompt, text, [this](const Result& result) {
        emit grammarCheckCompleted(result);
    });
}

void AIService::improveWriting(const QString& text)
{
    const QString systemPrompt = QStringLiteral(
        R"(You are a writing assistant.
Improve the given text to be clearer and more engaging while preserving the original meaning.
- Enhance clarity and flow
- Fix any grammar or spelling issues
- Keep the same tone and intent
- Keep the same language as the input

Respond with ONLY the improved text, no explanations.)");

    sendRequest(systemPrompt, text, [this](const Result& result) {
        emit textImprovementCompleted(result);
    });
}

void AIService::makeMoreFormal(const QString& text)
{
    const QString systemPrompt = QStringLiteral(
        R"(You are a professional writing assistant.
Rewrite the given text in a more formal, professional tone suitable for business communication.
- Use professional language
- Fix any grammar or spelling issues
- Maintain the original meaning
- Keep the same language as the input

Respond with ONLY the formal version, no explanations.)");

    sendRequest(systemPrompt, text, [this](const Result& result) {
        emit textImprovementCompleted(result);
    });
}

void AIService::makeConcise(const QString& text)
{
    const QString systemPrompt = QStringLiteral(
        R"(You are a concise writing assistant.
Rewrite the given text to be shorter and more direct while keeping the essential meaning.
- Remove unnecessary words and redundancy
- Keep the key points
- Fix any grammar issues
- Keep the same language as the input

Respond with ONLY the concise version, no explanations.)");

    sendRequest(systemPrompt, text, [this](const Result& result) {
        emit textImprovementCompleted(result);
    });
}

void AIService::sendRequest(const QString& systemPrompt, const QString& userText,
                            std::function<void(const Result&)> callback)
{
    if (!isConfigured()) {
        Result result;
        result.success = false;
        result.errorMessage =
            QStringLiteral("AI API key not configured. Open Settings and add your Google AI Studio key.");
        result.originalText = userText;
        callback(result);
        emit errorOccurred(result.errorMessage);
        return;
    }

    if (userText.trimmed().isEmpty()) {
        Result result;
        result.success = false;
        result.errorMessage = QStringLiteral("No text to check.");
        result.originalText = userText;
        callback(result);
        return;
    }

    if (m_providerKind != AiProviderKind::Gemini) {
        Result result;
        result.success = false;
        result.errorMessage = QStringLiteral("Only Gemini is supported in this version.");
        result.originalText = userText;
        callback(result);
        emit errorOccurred(result.errorMessage);
        return;
    }

    const QString url = GeminiProvider::buildUrl(m_apiKey);
    const QByteArray body = GeminiProvider::buildRequestBody(systemPrompt, userText);
    QNetworkReply* reply = GeminiProvider::post(m_networkManager, url, body);

    connect(reply, &QNetworkReply::finished, this, [this, reply, userText, callback]() {
        handleResponse(reply, userText, callback);
    });
}

void AIService::handleResponse(QNetworkReply* reply, const QString& originalText,
                               std::function<void(const Result&)> callback)
{
    Result result;
    result.originalText = originalText;

    const QByteArray responseData = reply->readAll();

    QString outText;
    QString parseErr;
    const bool parsedOk = GeminiProvider::parseResponse(responseData, outText, parseErr);

    if (reply->error() != QNetworkReply::NoError) {
        result.success = false;
        result.errorMessage = !parseErr.isEmpty() ? parseErr : reply->errorString();
        qWarning() << "[AI] Request failed:" << result.errorMessage;
        emit errorOccurred(result.errorMessage);
    } else if (parsedOk) {
        result.improvedText = outText;
        result.success = true;
        qDebug() << "[AI] Response received, length:" << result.improvedText.length();
    } else {
        result.success = false;
        result.errorMessage = parseErr.isEmpty() ? QStringLiteral("Invalid response format") : parseErr;
        emit errorOccurred(result.errorMessage);
    }

    reply->deleteLater();
    callback(result);
}

} // namespace aurora::mail::app::ai
