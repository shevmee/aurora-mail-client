#include "PkceSession.hpp"

#include <QByteArray>

static const int VERIFIER_ENTROPY_BYTES = 64;
static const int STATE_ENTROPY_BYTES = 32;

PkceSession::PkceSession()
    : codeVerifier_(generateRandomUrlSafeString(VERIFIER_ENTROPY_BYTES)),
      codeChallenge_(calculateS256Challenge(codeVerifier_)),
      state_(generateRandomUrlSafeString(STATE_ENTROPY_BYTES))
{
}

void PkceSession::appendAuthParameters(QUrlQuery& query) const
{
  query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge_);
  query.addQueryItem(QStringLiteral("code_challenge_method"), codeChallengeMethod());
  query.addQueryItem(QStringLiteral("state"), state_);
}

bool PkceSession::validateState(const QString& incomingState) const
{
  return !incomingState.isEmpty() && (state_ == incomingState);
}

QString PkceSession::codeChallengeMethod() const
{
  return QStringLiteral("S256");
}

QString PkceSession::calculateS256Challenge(const QString& verifier)
{
  QByteArray hash = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);

  return hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString PkceSession::generateRandomUrlSafeString(qsizetype lengthInBytes)
{
  QByteArray randomBytes(lengthInBytes, Qt::Uninitialized);

  quint32* ptr = reinterpret_cast<quint32*>(randomBytes.data());
  QRandomGenerator::system()->fillRange(ptr, lengthInBytes / 4);

  // Base64UrlEncoding replaces '+' to '-' and '/' to '_' which is safe for Uniform Resource Locator(URL)
  // OmitTrailingEquals removes '=' and the end of the Base64 string (RFC requirement for PKCE)
  return randomBytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
