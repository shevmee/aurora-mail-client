#ifndef PKCESESSION_HPP
#define PKCESESSION_HPP

#include <QString>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrlQuery>

class PkceSession {
public:
    PkceSession();
    ~PkceSession() = default;

    // Non-copyable, movable (contains unique session state)
    Q_DISABLE_COPY(PkceSession)
    PkceSession(PkceSession&&) noexcept = default;
    PkceSession& operator=(PkceSession&&) noexcept = default;

    [[nodiscard]] QString codeVerifier() const { return codeVerifier_; }
    [[nodiscard]] QString codeChallenge() const { return codeChallenge_; }
    [[nodiscard]] QString state() const { return state_; }

    [[nodiscard]] QString codeChallengeMethod() const;
    void appendAuthParameters(QUrlQuery& query) const;
    [[nodiscard]] bool validateState(const QString& incomingState) const;

private:
    static QString generateRandomUrlSafeString(qsizetype lengthInBytes);
    static QString calculateS256Challenge(const QString& verifier);

private:
    QString codeVerifier_;
    QString codeChallenge_;
    QString state_;
};

#endif // PKCESESSION_HPP
