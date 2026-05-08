#include "GeminiProvider.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

QString GeminiProvider::buildUrl(const QString& /*apiKey*/, const QString& modelPath)
{
  const QString path = modelPath.isEmpty() ? QString::fromLatin1(kDefaultModelPath) : modelPath;
  QUrl url(QString::fromLatin1("https://generativelanguage.googleapis.com/") + path);
  return url.toString();
}

QByteArray GeminiProvider::buildRequestBody(const QString& systemPrompt, const QString& userText)
{
  QJsonObject systemPart;
  systemPart[QStringLiteral("text")] = systemPrompt;

  QJsonArray systemParts;
  systemParts.append(systemPart);

  QJsonObject systemInstruction;
  systemInstruction[QStringLiteral("parts")] = systemParts;

  QJsonObject userPart;
  userPart[QStringLiteral("text")] = userText;

  QJsonArray userParts;
  userParts.append(userPart);

  QJsonObject userContent;
  userContent[QStringLiteral("parts")] = userParts;

  QJsonArray contents;
  contents.append(userContent);

  QJsonObject generationConfig;
  generationConfig[QStringLiteral("temperature")] = 0.3;
  generationConfig[QStringLiteral("maxOutputTokens")] = 2000;

  QJsonObject requestBody;
  requestBody[QStringLiteral("contents")] = contents;
  requestBody[QStringLiteral("systemInstruction")] = systemInstruction;
  requestBody[QStringLiteral("generationConfig")] = generationConfig;

  return QJsonDocument(requestBody).toJson(QJsonDocument::Compact);
}

QNetworkReply*
GeminiProvider::post(QNetworkAccessManager* nam, const QString& urlString, const QString& apiKey, const QByteArray& jsonBody)
{
  QNetworkRequest request{ QUrl{ urlString } };
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("x-goog-api-key", apiKey.toUtf8());
  return nam->post(request, jsonBody);
}

bool GeminiProvider::parseResponse(const QByteArray& data, QString& outText, QString& outError)
{
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject())
  {
    outError = QStringLiteral("Invalid response format");
    return false;
  }

  QJsonObject obj = doc.object();
  if (obj.contains(QStringLiteral("error")))
  {
    QJsonObject error = obj[QStringLiteral("error")].toObject();
    outError = error[QStringLiteral("message")].toString();
    return false;
  }

  QJsonArray candidates = obj[QStringLiteral("candidates")].toArray();
  if (candidates.isEmpty())
  {
    if (obj.contains(QStringLiteral("promptFeedback")))
    {
      QJsonObject feedback = obj[QStringLiteral("promptFeedback")].toObject();
      if (feedback.contains(QStringLiteral("blockReason")))
      {
        outError = QStringLiteral("Content blocked: ") + feedback[QStringLiteral("blockReason")].toString();
        return false;
      }
    }
    outError = QStringLiteral("No response from AI");
    return false;
  }

  QJsonObject candidate = candidates[0].toObject();
  QJsonObject content = candidate[QStringLiteral("content")].toObject();
  QJsonArray parts = content[QStringLiteral("parts")].toArray();
  if (parts.isEmpty())
  {
    outError = QStringLiteral("No text in AI response");
    return false;
  }

  QJsonObject part = parts[0].toObject();
  outText = part[QStringLiteral("text")].toString().trimmed();
  return !outText.isEmpty();
}
