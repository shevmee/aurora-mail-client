#include "OAuthManager.hpp"

#include <QDebug>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

OAuthManager::OAuthManager(Provider provider, QObject* parent)
    : QObject(parent),
      config_(OAuthConfigFactory::create(provider)),
      redirect_server_(this)
{
  setupConnections();
}

OAuthManager::OAuthManager(const OAuthConfig& config, QObject* parent)
    : QObject(parent),
      config_(config),
      redirect_server_(this)
{
  setupConnections();
}

void OAuthManager::setupConnections()
{
  // Connect redirect server signals
  connect(&redirect_server_, &OAuthRedirectServer::authCodeReceived, this, &OAuthManager::handleAuthorizationCode);
  connect(&redirect_server_, &OAuthRedirectServer::errorReceived, this, &OAuthManager::authenticationFailed);
  connect(&redirect_server_, &OAuthRedirectServer::serverError, this, &OAuthManager::authenticationFailed);
}

void OAuthManager::setCredentials(const QString& clientId, const QString& clientSecret)
{
  config_.clientId = clientId;
  config_.clientSecret = clientSecret;
}

void OAuthManager::startAuthFlow()
{
  // Stop any existing server
  stopAuthServer();

  // Start the redirect server on an ephemeral port
  if (!redirect_server_.listen(0))
  {
    // Error already emitted by serverError signal
    return;
  }

  // Update redirect URI with the actual port
  config_.redirectUri = QString("http://127.0.0.1:%1").arg(redirect_server_.serverPort());

  // Get the authorization URL and open in browser
  QUrl authUrl = getAuthorizationUrl();

  if (!QDesktopServices::openUrl(authUrl))
  {
    stopAuthServer();
    emit authenticationFailed("Failed to open browser for authentication");
    return;
  }
}

void OAuthManager::stopAuthServer()
{
  redirect_server_.close();
}

QUrl OAuthManager::getAuthorizationUrl()
{
  current_session_ = std::make_unique<PkceSession>();

  QUrl url(config_.authorizationEndpoint);
  QUrlQuery query;

  query.addQueryItem("client_id", config_.clientId);
  query.addQueryItem("redirect_uri", config_.redirectUri);
  query.addQueryItem("response_type", "code");
  query.addQueryItem("scope", config_.scopes.join(" "));
  query.addQueryItem("access_type", "offline");  // Request refresh token
  query.addQueryItem("prompt", "consent");       // Always show consent screen

  current_session_->appendAuthParameters(query);

  url.setQuery(query);
  return url;
}

void OAuthManager::handleAuthorizationCode(const QString& code, const QString& state)
{
  if (current_session_ == nullptr)
  {
    emit authenticationFailed("No active authentication session found");
    return;
  }
  // Verify state for CSRF protection
  if (!current_session_->validateState(state))
  {
    emit authenticationFailed("Security Alert: State mismatch (possible CSRF attack)");
    return;
  }

  exchangeCodeForTokens(code);
}

void OAuthManager::exchangeCodeForTokens(const QString& code)
{
  QNetworkRequest request(QUrl(config_.tokenEndpoint));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  QUrlQuery postData;
  postData.addQueryItem("client_id", config_.clientId);
  postData.addQueryItem("code", code);
  postData.addQueryItem("redirect_uri", config_.redirectUri);
  postData.addQueryItem("grant_type", "authorization_code");
  postData.addQueryItem("code_verifier", current_session_->codeVerifier());

  // Google requires client_secret even with PKCE for Desktop app type
  if (!config_.clientSecret.isEmpty())
  {
    postData.addQueryItem("client_secret", config_.clientSecret);
  }

  QNetworkReply* reply = network_manager_.post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

  connect(
      reply,
      &QNetworkReply::finished,
      this,
      [this, reply]()
      {
        QByteArray responseData = reply->readAll();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
          QString errorMsg = reply->errorString();
          if (!responseData.isEmpty())
          {
            QJsonDocument errorDoc = QJsonDocument::fromJson(responseData);
            if (errorDoc.isObject())
            {
              QJsonObject errorJson = errorDoc.object();
              if (errorJson.contains("error_description"))
              {
                errorMsg = errorJson["error_description"].toString();
              }
              else if (errorJson.contains("error"))
              {
                errorMsg = errorJson["error"].toString();
              }
            }
          }
          emit authenticationFailed(errorMsg);
          return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject json = doc.object();

        if (json.contains("error"))
        {
          QString errorMsg = json["error_description"].toString();
          if (errorMsg.isEmpty())
          {
            errorMsg = json["error"].toString();
          }
          emit authenticationFailed(errorMsg);
          return;
        }

        // Parse token response
        tokens_.accessToken = json["access_token"].toString();
        tokens_.refreshToken = json["refresh_token"].toString();
        tokens_.tokenType = json["token_type"].toString("Bearer");
        tokens_.scope = json["scope"].toString();

        int expiresIn = json["expires_in"].toInt(3600);
        tokens_.expiresAt = QDateTime::currentDateTime().addSecs(expiresIn - 60);  // 60s buffer

        // Extract email from id_token (JWT)
        QString idToken = json["id_token"].toString();
        if (!idToken.isEmpty())
        {
          QStringList parts = idToken.split('.');
          if (parts.size() >= 2)
          {
            // Decode the payload (second part of JWT)
            QByteArray payload = QByteArray::fromBase64(parts[1].toUtf8(), QByteArray::Base64UrlEncoding);
            QJsonDocument payloadDoc = QJsonDocument::fromJson(payload);
            if (payloadDoc.isObject())
            {
              QJsonObject payloadJson = payloadDoc.object();
              user_email_ = payloadJson["email"].toString();
              // Do not log email (PII); OAuth state is enough for debugging.
              qDebug() << "OAuth: id_token parsed, account identity set";
            }
          }
        }

        // Scope subsequent persistence to the account just authenticated so
        // that signing in with a second Google account does not overwrite the
        // tokens of the first one.
        if (!user_email_.isEmpty())
        {
          token_storage_.setAccountKey(user_email_);
        }

        // Save tokens
        saveTokens();

        qDebug() << "OAuth authentication successful!";
        emit authenticated();
      });
}

std::optional<std::string> OAuthManager::getAccessToken()
{
  if (!isAuthenticated())
  {
    return std::nullopt;
  }
  if (tokens_.isExpired())
  {
    return std::nullopt;
  }
  return tokens_.accessToken.toStdString();
}

void OAuthManager::ensureValidAccessToken(std::function<void(std::optional<std::string>)> callback)
{
  if (!isAuthenticated())
  {
    callback(std::nullopt);
    return;
  }
  if (!tokens_.isExpired())
  {
    callback(tokens_.accessToken.toStdString());
    return;
  }
  if (!tokens_.canRefresh())
  {
    callback(std::nullopt);
    return;
  }
  refreshAccessTokenAsync(
      [this, cb = std::move(callback)](bool success) mutable
      {
        if (!success)
        {
          cb(std::nullopt);
          return;
        }
        cb(tokens_.accessToken.toStdString());
      });
}

void OAuthManager::refreshAccessTokenAsync(std::function<void(bool success)> onComplete)
{
  if (!tokens_.canRefresh())
  {
    qWarning() << "No refresh token available for async refresh";
    onComplete(false);
    return;
  }

  if (config_.clientId.isEmpty())
  {
    qWarning() << "Client ID not set - cannot refresh token";
    onComplete(false);
    return;
  }

  QNetworkRequest request(QUrl(config_.tokenEndpoint));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  QUrlQuery postData;
  postData.addQueryItem("client_id", config_.clientId);
  postData.addQueryItem("refresh_token", tokens_.refreshToken);
  postData.addQueryItem("grant_type", "refresh_token");

  if (!config_.clientSecret.isEmpty())
  {
    postData.addQueryItem("client_secret", config_.clientSecret);
  }

  qDebug() << "Attempting token refresh (async)...";

  QNetworkReply* reply = network_manager_.post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

  connect(
      reply,
      &QNetworkReply::finished,
      this,
      [this, reply, onComplete = std::move(onComplete)]() mutable
      {
        const QNetworkReply::NetworkError err = reply->error();
        const QString errString = reply->errorString();
        QByteArray responseData = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        qDebug() << "Token refresh HTTP status:" << httpStatus;

        if (err != QNetworkReply::NoError)
        {
          qWarning() << "Token refresh failed:" << errString;
          qWarning() << "Response:" << responseData;
          onComplete(false);
          return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject json = doc.object();

        if (json.contains("error"))
        {
          QString errorMsg = json["error_description"].toString();
          if (errorMsg.isEmpty())
          {
            errorMsg = json["error"].toString();
          }
          qWarning() << "Token refresh error:" << errorMsg;
          onComplete(false);
          return;
        }

        tokens_.accessToken = json["access_token"].toString();

        if (json.contains("refresh_token"))
        {
          tokens_.refreshToken = json["refresh_token"].toString();
        }

        int expiresIn = json["expires_in"].toInt(3600);
        tokens_.expiresAt = QDateTime::currentDateTime().addSecs(expiresIn - 60);

        saveTokens();

        qDebug() << "Token refreshed successfully";
        onComplete(true);
      });
}

std::function<std::optional<std::string>()> OAuthManager::createTokenProvider()
{
  return [this]() -> std::optional<std::string> { return getAccessToken(); };
}

bool OAuthManager::isAuthenticated() const
{
  return !tokens_.accessToken.isEmpty() && (!tokens_.isExpired() || tokens_.canRefresh());
}

void OAuthManager::signOut()
{
  // Capture the refresh token BEFORE local cleanup so we can ask the provider
  // to invalidate it server-side. Per RFC 7009 §2.2 this is best-effort: a
  // failed/slow network response must not delay or block local sign-out.
  const QString refreshToken = tokens_.refreshToken;

  tokens_ = TokenData{};
  user_email_.clear();
  token_storage_.clear();

  revokeRefreshTokenBestEffort(refreshToken);

  emit signedOut();
}

void OAuthManager::revokeRefreshTokenBestEffort(const QString& refreshToken)
{
  if (refreshToken.isEmpty() || config_.revocationEndpoint.isEmpty())
  {
    return;
  }

  QNetworkRequest request(QUrl(config_.revocationEndpoint));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  QUrlQuery postData;
  postData.addQueryItem("token", refreshToken);
  postData.addQueryItem("token_type_hint", "refresh_token");

  qDebug() << "Revoking refresh_token at" << config_.revocationEndpoint << "(best effort)";

  QNetworkReply* reply = network_manager_.post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

  // Fire-and-forget: log the outcome but do not block the caller. The reply
  // outlives this function and is owned by network_manager_; deleteLater()
  // releases it once the response completes (or the QNAM is destroyed).
  connect(
      reply,
      &QNetworkReply::finished,
      this,
      [reply]()
      {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto err = reply->error();
        if (err == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300)
        {
          qDebug() << "refresh_token revocation acknowledged (HTTP" << httpStatus << ")";
        }
        else
        {
          // RFC 7009 §2.2: clients SHOULD continue with local sign-out even
          // when the server responds with an error. We log and move on.
          qWarning() << "refresh_token revocation failed (HTTP" << httpStatus << ", err"
                     << err << "); local sign-out has already completed";
        }
        reply->deleteLater();
      });
}

bool OAuthManager::loadStoredTokens()
{
  auto storedTokens = token_storage_.load();
  if (!storedTokens)
  {
    return false;
  }

  tokens_ = *storedTokens;
  user_email_ = tokens_.email;

  return isAuthenticated();
}

void OAuthManager::saveTokens()
{
  tokens_.email = user_email_;
  token_storage_.save(tokens_);
}

void OAuthManager::setAccountIdentity(const QString& email)
{
  token_storage_.setAccountKey(email);
  // Note: do NOT mutate user_email_ here — the caller may set it earlier
  // (so the storage scope matches) before any tokens are loaded; the
  // canonical user identity for the in-memory session is updated by
  // loadStoredTokens() / exchangeCodeForTokens() / setUserEmail().
}

void OAuthManager::clearInMemorySession()
{
  tokens_ = TokenData{};
  user_email_.clear();
}
