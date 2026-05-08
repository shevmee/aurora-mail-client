#ifndef TOKENSTORAGE_HPP
#define TOKENSTORAGE_HPP

#include <QString>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

// Include backend headers
#include "Storage/SettingsBackend.hpp"
#if AURORA_USE_KEYCHAIN
#include "Storage/KeychainBackend.hpp"
#endif

/**
 * @brief Compile-time backend selection.
 * 
 * Uses KeychainBackend when AURORA_USE_KEYCHAIN=ON, otherwise SettingsBackend.
 */
#if AURORA_USE_KEYCHAIN
using SecureStorageBackend = KeychainBackend;
#else
using SecureStorageBackend = SettingsBackend;
#endif

/**
 * @class TokenStorage
 * @brief Handles secure storage of OAuth tokens.
 *
 * All token data is serialized to JSON and stored via the selected backend.
 * No direct QSettings usage — storage is fully delegated to the backend.
 *
 * Storage backend is selected at compile time via AURORA_USE_KEYCHAIN:
 * - When enabled: Uses KeychainBackend (macOS Keychain, Windows Credential Store, libsecret)
 * - When disabled: Uses SettingsBackend (QSettings - development only, NOT SECURE)
 *
 * @note Build with -DAURORA_USE_KEYCHAIN=ON for production use.
 * 
 * @tparam Backend Storage backend type (defaults to compile-time selected backend).
 */
template<SecureStorageConcept Backend = SecureStorageBackend>
class TokenStorageT
{
public:
    /**
     * @brief OAuth token data structure.
     */
    struct TokenData {
        QString accessToken;         ///< Access token for API calls
        QString refreshToken;        ///< Refresh token for obtaining new access tokens
        QDateTime expiresAt;         ///< When the access token expires
        QString tokenType{"Bearer"}; ///< Token type (typically "Bearer")
        QString scope;               ///< Granted OAuth scopes
        QString email;               ///< User email associated with tokens

        [[nodiscard]] bool isExpired() const noexcept {
            return QDateTime::currentDateTime() >= expiresAt;
        }

        [[nodiscard]] bool isValid() const noexcept {
            return !accessToken.isEmpty() && !isExpired();
        }

        [[nodiscard]] bool canRefresh() const noexcept {
            return !refreshToken.isEmpty();
        }

        /**
         * @brief Serializes token data to JSON string.
         */
        [[nodiscard]] QString toJson() const {
            QJsonObject obj;
            obj[QLatin1String("accessToken")] = accessToken;
            obj[QLatin1String("refreshToken")] = refreshToken;
            obj[QLatin1String("expiresAt")] = expiresAt.toString(Qt::ISODate);
            obj[QLatin1String("tokenType")] = tokenType;
            obj[QLatin1String("scope")] = scope;
            obj[QLatin1String("email")] = email;
            return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        }

        /**
         * @brief Deserializes token data from JSON string.
         * @return TokenData if parsing succeeds, std::nullopt otherwise.
         */
        [[nodiscard]] static std::optional<TokenData> fromJson(const QString& json) {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(json.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                return std::nullopt;
            }

            const auto obj = doc.object();
            TokenData tokens;
            tokens.accessToken = obj[QLatin1String("accessToken")].toString();
            tokens.refreshToken = obj[QLatin1String("refreshToken")].toString();
            tokens.expiresAt = QDateTime::fromString(obj[QLatin1String("expiresAt")].toString(), Qt::ISODate);
            tokens.tokenType = obj[QLatin1String("tokenType")].toString(QStringLiteral("Bearer"));
            tokens.scope = obj[QLatin1String("scope")].toString();
            tokens.email = obj[QLatin1String("email")].toString();

            if (tokens.accessToken.isEmpty()) {
                return std::nullopt;
            }

            return tokens;
        }
    };

    /**
     * @brief Constructs TokenStorage with application identifiers.
     * @param organization Organization name for storage.
     * @param application Application name for storage.
     */
    explicit TokenStorageT(const QString& organization = QStringLiteral("AuroraMail"),
                           const QString& application = QStringLiteral("OAuth"))
        : backend_(organization, application) {}

    // Non-copyable, movable
    TokenStorageT(const TokenStorageT&) = delete;
    TokenStorageT& operator=(const TokenStorageT&) = delete;
    TokenStorageT(TokenStorageT&&) = default;
    TokenStorageT& operator=(TokenStorageT&&) = default;
    ~TokenStorageT() = default;

    /**
     * @brief Selects which account this storage instance reads/writes for.
     *
     * Empty email targets the legacy single-account key (back-compat with
     * installations that pre-date multi-account support). A non-empty email
     * scopes the secret to that account so multiple accounts can coexist
     * in the same secure backend without overwriting each other.
     */
    void setAccountKey(const QString& email)
    {
        account_email_ = email;
    }

    /// Returns the email currently used to scope storage operations.
    [[nodiscard]] QString accountKey() const { return account_email_; }

    /**
     * @brief Loads tokens from storage.
     * @return TokenData if tokens exist and are loadable, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<TokenData> load() const
    {
        const QString json = backend_.retrieve(currentKey());
        if (json.isEmpty()) {
            return std::nullopt;
        }
        return TokenData::fromJson(json);
    }

    /**
     * @brief Saves tokens to storage.
     * @param tokens Token data to persist.
     */
    void save(const TokenData& tokens)
    {
        backend_.store(currentKey(), tokens.toJson());
    }

    /**
     * @brief Clears stored tokens for the currently selected account.
     */
    void clear()
    {
        backend_.remove(currentKey());
    }

    /**
     * @brief Checks if tokens exist for the currently selected account.
     * @return True if token storage contains data.
     */
    [[nodiscard]] bool exists() const
    {
        return !backend_.retrieve(currentKey()).isEmpty();
    }

    /**
     * @brief Returns whether secure storage (keychain) is enabled.
     * @return True if backend provides secure storage.
     */
    [[nodiscard]] static constexpr bool isSecureStorageEnabled() noexcept {
        return Backend::isSecure();
    }

private:
    [[nodiscard]] QString currentKey() const
    {
        if (account_email_.isEmpty()) {
            return QString::fromLatin1(LEGACY_STORAGE_KEY);
        }
        return QString::fromLatin1(STORAGE_KEY_PREFIX) + account_email_;
    }

    Backend backend_;

    // Per-account email used to scope the storage key. Empty means "use legacy
    // single-account key" (back-compat with pre-multi-account installations).
    QString account_email_;

    // Legacy single-account key (kept for migration of existing installs).
    static constexpr const char* LEGACY_STORAGE_KEY = "oauth_tokens";

    // Prefix for per-account keys. The full key is "<prefix><email>".
    static constexpr const char* STORAGE_KEY_PREFIX = "oauth_tokens:";
};

/**
 * @brief Default TokenStorage type using compile-time selected backend.
 */
using TokenStorage = TokenStorageT<SecureStorageBackend>;

#endif // TOKENSTORAGE_HPP
