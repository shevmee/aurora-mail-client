#ifndef PKCESESSION_HPP
#define PKCESESSION_HPP

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QString>
#include <QUrlQuery>

/**
 * @brief Per-flow PKCE state for an OAuth 2.0 Authorization Code grant.
 *
 * Implements RFC 7636 (Proof Key for Code Exchange):
 *   - generates a 32-byte cryptographically random `code_verifier`,
 *   - derives the SHA-256 `code_challenge` (method = `S256`),
 *   - generates a separate random `state` for CSRF protection on the
 *     redirect callback.
 *
 * One `PkceSession` represents exactly one in-flight authorization request;
 * it is non-copyable but movable so it can be owned by an `OAuthFlow` for the
 * lifetime of that single round-trip.
 */
class PkceSession
{
 public:
  /// Generates a fresh verifier, derived challenge, and state token.
  PkceSession();
  ~PkceSession() = default;

  // Non-copyable, movable (contains unique session state)
  Q_DISABLE_COPY(PkceSession)
  PkceSession(PkceSession&&) noexcept = default;
  PkceSession& operator=(PkceSession&&) noexcept = default;

  /// @returns The high-entropy code_verifier sent in the token-exchange step.
  [[nodiscard]] QString codeVerifier() const
  {
    return codeVerifier_;
  }
  /// @returns The base64url(SHA-256(code_verifier)) sent on the auth request.
  [[nodiscard]] QString codeChallenge() const
  {
    return codeChallenge_;
  }
  /// @returns The CSRF `state` token included on the auth request.
  [[nodiscard]] QString state() const
  {
    return state_;
  }

  /// @returns The PKCE method identifier (`S256`).
  [[nodiscard]] QString codeChallengeMethod() const;
  /// Appends `code_challenge`, `code_challenge_method`, and `state` to @p query.
  void appendAuthParameters(QUrlQuery& query) const;
  /// @returns `true` iff @p incomingState matches the stored CSRF state token.
  [[nodiscard]] bool validateState(const QString& incomingState) const;

 private:
  static QString generateRandomUrlSafeString(qsizetype lengthInBytes);
  static QString calculateS256Challenge(const QString& verifier);

 private:
  QString codeVerifier_;
  QString codeChallenge_;
  QString state_;
};

#endif  // PKCESESSION_HPP
