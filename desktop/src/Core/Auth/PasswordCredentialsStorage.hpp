#ifndef PASSWORD_CREDENTIALS_STORAGE_HPP
#define PASSWORD_CREDENTIALS_STORAGE_HPP

#include "Storage/SettingsBackend.hpp"
#if AURORA_USE_KEYCHAIN
#include "Storage/KeychainBackend.hpp"
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

#include <optional>

/**
 * @file PasswordCredentialsStorage.hpp
 * @brief Per-account secure storage for IMAP/SMTP password credentials.
 *
 * Stores email + IMAP/SMTP server hints + password as a JSON blob keyed by
 * the user's email address. Backed by the same compile-time selected secure
 * storage backend as TokenStorage.
 *
 * @par Security policy
 * Unlike TokenStorage (which the existing codebase tolerates falling back to
 * QSettings for OAuth tokens in development), this class will REFUSE to write
 * a password to disk unless the chosen backend reports `isSecure() == true`.
 * Long-lived account passwords are far more sensitive than short-lived OAuth
 * access tokens, so a silent QSettings fallback would be an unacceptable
 * security regression.
 *
 * Non-secret hints (last-used email, server hostnames) are mirrored into
 * QSettings so the login form can be pre-filled even when secure storage is
 * unavailable.
 */
class PasswordCredentialsStorage
{
public:
    /// Plaintext bundle held in memory only for the duration of an auth flow.
    struct Credentials
    {
        QString email;
        QString imapServer;
        quint16 imapPort{993};
        QString smtpServer;
        quint16 smtpPort{587};
        QString password;

        [[nodiscard]] bool isComplete() const noexcept
        {
            return !email.isEmpty() && !password.isEmpty()
                && !imapServer.isEmpty() && !smtpServer.isEmpty();
        }
    };

    explicit PasswordCredentialsStorage(
        const QString& organization = QStringLiteral("AuroraMail"),
        const QString& application = QStringLiteral("AuroraMail-PasswordAuth"))
        :
#if AURORA_USE_KEYCHAIN
          backend_(organization, application)
#else
          backend_(organization, application)
#endif
        , settings_(organization, QStringLiteral("AuroraMail-Login"))
    {
    }

    /**
     * @brief Returns true if password storage is backed by a secure store.
     * @note Use this to gate the "remember password" UI affordance.
     */
    [[nodiscard]] static constexpr bool isSecure() noexcept
    {
        return Backend::isSecure();
    }

    /**
     * @brief Persists the last email and (non-secret) server hints.
     *
     * Always safe to call; only writes the password when the backend is secure.
     * Returns true iff the password itself was persisted.
     */
    bool save(const Credentials& creds)
    {
        if (creds.email.isEmpty()) {
            return false;
        }
        rememberLoginHints(creds);

        if constexpr (!Backend::isSecure()) {
            // SECURITY: never write a password to QSettings.
            return false;
        }
        backend_.store(makeKey(creds.email), serialize(creds));
        return true;
    }

    /**
     * @brief Loads credentials previously stored for `email`, if any.
     */
    [[nodiscard]] std::optional<Credentials> load(const QString& email) const
    {
        if (email.isEmpty() || !Backend::isSecure()) {
            return std::nullopt;
        }
        const QString json = backend_.retrieve(makeKey(email));
        if (json.isEmpty()) {
            return std::nullopt;
        }
        return deserialize(json);
    }

    /// Removes the stored secret for `email`. Hints in QSettings are kept.
    void remove(const QString& email)
    {
        if (email.isEmpty() || !Backend::isSecure()) {
            return;
        }
        backend_.remove(makeKey(email));
    }

    /// Returns the most recently used email, or an empty string.
    [[nodiscard]] QString lastEmail() const
    {
        return settings_.value(QStringLiteral("lastEmail")).toString();
    }

    /// Returns the most recently used server hints (non-secret).
    struct ServerHints
    {
        QString imapServer;
        quint16 imapPort{993};
        QString smtpServer;
        quint16 smtpPort{587};
    };

    [[nodiscard]] ServerHints lastServerHints() const
    {
        ServerHints h;
        h.imapServer = settings_.value(QStringLiteral("imapServer"),
                                       QStringLiteral("imap.gmail.com")).toString();
        h.imapPort = static_cast<quint16>(
            settings_.value(QStringLiteral("imapPort"), 993).toUInt());
        h.smtpServer = settings_.value(QStringLiteral("smtpServer"),
                                       QStringLiteral("smtp.gmail.com")).toString();
        h.smtpPort = static_cast<quint16>(
            settings_.value(QStringLiteral("smtpPort"), 587).toUInt());
        return h;
    }

private:
#if AURORA_USE_KEYCHAIN
    using Backend = KeychainBackend;
#else
    using Backend = SettingsBackend;
#endif

    static QString makeKey(const QString& email)
    {
        return QStringLiteral("password_creds:") + email;
    }

    static QString serialize(const Credentials& creds)
    {
        QJsonObject obj;
        obj[QLatin1String("email")] = creds.email;
        obj[QLatin1String("imapServer")] = creds.imapServer;
        obj[QLatin1String("imapPort")] = creds.imapPort;
        obj[QLatin1String("smtpServer")] = creds.smtpServer;
        obj[QLatin1String("smtpPort")] = creds.smtpPort;
        obj[QLatin1String("password")] = creds.password;
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    static std::optional<Credentials> deserialize(const QString& json)
    {
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return std::nullopt;
        }
        const auto obj = doc.object();
        Credentials c;
        c.email = obj[QLatin1String("email")].toString();
        c.imapServer = obj[QLatin1String("imapServer")].toString();
        c.imapPort = static_cast<quint16>(obj[QLatin1String("imapPort")].toInt(993));
        c.smtpServer = obj[QLatin1String("smtpServer")].toString();
        c.smtpPort = static_cast<quint16>(obj[QLatin1String("smtpPort")].toInt(587));
        c.password = obj[QLatin1String("password")].toString();
        if (c.email.isEmpty() || c.password.isEmpty()) {
            return std::nullopt;
        }
        return c;
    }

    void rememberLoginHints(const Credentials& creds)
    {
        settings_.setValue(QStringLiteral("lastEmail"), creds.email);
        settings_.setValue(QStringLiteral("imapServer"), creds.imapServer);
        settings_.setValue(QStringLiteral("imapPort"), creds.imapPort);
        settings_.setValue(QStringLiteral("smtpServer"), creds.smtpServer);
        settings_.setValue(QStringLiteral("smtpPort"), creds.smtpPort);
    }

    Backend backend_;
    mutable QSettings settings_;
};

#endif  // PASSWORD_CREDENTIALS_STORAGE_HPP
