#ifndef OAUTHMANAGER_HPP
#define OAUTHMANAGER_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUrl>
#include <QNetworkAccessManager>

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "PkceSession.hpp"
#include "TokenStorage.hpp"
#include "OAuthConfig.hpp"
#include "OAuthRedirectServer.hpp"

/**
 * @class OAuthManager
 * @brief Manages OAuth 2.0 authentication flow for email services.
 *
 * Supports the OAuth 2.0 authorization code flow with PKCE for desktop apps.
 * Stores tokens in system keychain (macOS Keychain, Windows Credential Manager, etc.)
 * via Qt's QKeychain or secure storage.
 */
class OAuthManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief OAuth token data (alias to TokenStorage::TokenData).
     */
    using TokenData = TokenStorage::TokenData;

    /**
     * @brief Constructs OAuthManager with a provider.
     * @param provider The OAuth provider to use.
     * @param parent Parent QObject.
     */
    explicit OAuthManager(Provider provider = Provider::Gmail, QObject* parent = nullptr);

    /**
     * @brief Constructs OAuthManager with custom configuration.
     * @param config Custom OAuth configuration.
     * @param parent Parent QObject.
     */
    explicit OAuthManager(const OAuthConfig& config, QObject* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~OAuthManager() override = default;

    // Non-copyable (QObject semantics)
    Q_DISABLE_COPY_MOVE(OAuthManager)

    /**
     * @brief Sets the OAuth client credentials.
     * @param clientId OAuth client ID from Google Cloud Console.
     * @param clientSecret OAuth client secret (required for refresh tokens).
     */
    void setCredentials(const QString& clientId, const QString& clientSecret = QString());

    /**
     * @brief Starts the complete OAuth flow automatically.
     * 
     * This starts a local server, opens the browser, and handles the callback.
     * Emits authenticated() or authenticationFailed() when complete.
     */
    void startAuthFlow();

    /**
     * @brief Stops the OAuth callback server if running.
     */
    void stopAuthServer();

    /**
     * @brief Gets the authorization URL for user login.
     * 
     * Open this URL in a browser for the user to authorize.
     * 
     * @return Authorization URL.
     */
    QUrl getAuthorizationUrl();

    /**
     * @brief Handles the authorization code from the OAuth callback.
     * 
     * Call this when the user is redirected back with an authorization code.
     * 
     * @param code The authorization code.
     * @param state The state parameter (for CSRF protection).
     */
    void handleAuthorizationCode(const QString& code, const QString& state = QString());

    /**
     * @brief Returns the current access token if it is still valid (not expired).
     *
     * Does not refresh. If the token is expired but refreshable, returns nullopt;
     * use ensureValidAccessToken() first.
     */
    std::optional<std::string> getAccessToken();

    /**
     * @brief Ensures a non-expired access token, refreshing asynchronously when needed.
     *
     * Must be called from the Qt thread that owns this QObject. The callback is
     * invoked on that thread when the token is ready or on failure.
     */
    void ensureValidAccessToken(std::function<void(std::optional<std::string>)> callback);

    /**
     * @brief Creates a token provider function for AuthXOAuth2.
     *
     * Returns getAccessToken() only; does not trigger refresh. For mail connect,
     * prefer ensureValidAccessToken() then pass a fixed token or refresh-aware provider.
     */
    std::function<std::optional<std::string>()> createTokenProvider();

    /**
     * @brief Checks if the user is authenticated.
     * @return True if valid tokens exist.
     */
    [[nodiscard]] bool isAuthenticated() const;

    /**
     * @brief Gets the authenticated user's email (if available).
     * @return Email address or empty string.
     */
    [[nodiscard]] QString getUserEmail() const { return user_email_; }

    /**
     * @brief Sets the user email (for storage purposes).
     */
    void setUserEmail(const QString& email) { user_email_ = email; }

    /**
     * @brief Clears all stored tokens for the currently targeted account
     *        and signs out of the in-memory session.
     */
    void signOut();

    /**
     * @brief Loads stored tokens from secure storage.
     * @return True if tokens were loaded successfully.
     */
    bool loadStoredTokens();

    /**
     * @brief Saves tokens to secure storage.
     */
    void saveTokens();

    /**
     * @brief Selects which account this manager reads/writes tokens for.
     *
     * Empty email targets the legacy single-account key (back-compat). Use
     * before @c loadStoredTokens(), @c saveTokens(), or @c signOut() to scope
     * those operations to a specific account in a multi-account install.
     */
    void setAccountIdentity(const QString& email);

    /**
     * @brief Drops in-memory tokens and user identity without touching storage.
     *
     * Use this when switching active accounts: we want the next
     * @c loadStoredTokens() / @c startAuthFlow() to start from a clean slate
     * but must NOT delete any other account's persisted tokens.
     */
    void clearInMemorySession();

signals:
    /**
     * @brief Emitted when authentication is successful.
     */
    void authenticated();

    /**
     * @brief Emitted when authentication fails.
     * @param error Error message.
     */
    void authenticationFailed(const QString& error);

    /**
     * @brief Emitted when user signs out.
     */
    void signedOut();

private:
    /**
     * @brief Exchanges authorization code for tokens.
     */
    void exchangeCodeForTokens(const QString& code);
    
    /**
     * @brief Refreshes the access token asynchronously (Qt thread).
     */
    void refreshAccessTokenAsync(std::function<void(bool success)> onComplete);

    /**
     * @brief Sets up connections for the OAuth redirect server.
     */
    void setupConnections();

private:
    OAuthConfig config_;
    TokenData tokens_;
    QString user_email_;
    
    std::unique_ptr<PkceSession> current_session_;
    
    // Token storage handler
    TokenStorage token_storage_;
    
    // Reusable network manager (connection pooling, keep-alive)
    QNetworkAccessManager network_manager_;
    
    // Local OAuth redirect server
    OAuthRedirectServer redirect_server_;
};

#endif // OAUTHMANAGER_HPP
