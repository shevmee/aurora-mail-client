#ifndef OAUTHCONFIGFACTORY_HPP
#define OAUTHCONFIGFACTORY_HPP

#include <QString>
#include <QStringList>

/**
 * @brief Supported OAuth providers.
 */
enum class Provider {
    Gmail,      ///< Google/Gmail OAuth
    Outlook,    ///< Microsoft/Outlook OAuth
    Custom      ///< Custom OAuth server
};

/**
 * @brief OAuth configuration for a provider.
 */
struct OAuthConfig {
    QString clientId;
    QString clientSecret;
    QString authorizationEndpoint;
    QString tokenEndpoint;
    // For desktop apps, use loopback redirect
    // MUST match exactly what's configured in Google Cloud Console
    QString redirectUri{"http://127.0.0.1"};
    QStringList scopes;
};

/**
 * @brief Factory for creating OAuth configurations for different providers.
 */
class OAuthConfigFactory {
public:
    OAuthConfigFactory() = delete;

    /**
     * @brief Creates a default OAuth configuration for the specified provider.
     * @param provider The OAuth provider.
     * @return OAuthConfig with provider-specific endpoints and scopes.
     */
    static OAuthConfig create(Provider provider) {
        OAuthConfig config;

        switch (provider) {
        case Provider::Gmail:
            config.authorizationEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
            config.tokenEndpoint = "https://oauth2.googleapis.com/token";
            config.scopes = {
                "https://mail.google.com/",  // Full Gmail access (IMAP/SMTP XOAUTH2)
                "openid",
                "email",
                "profile"
            };
            break;

        case Provider::Outlook:
            config.authorizationEndpoint = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
            config.tokenEndpoint = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
            config.scopes = {
                "https://outlook.office.com/IMAP.AccessAsUser.All",
                "https://outlook.office.com/SMTP.Send",
                "offline_access",
                "openid",
                "email",
                "profile"
            };
            break;

        case Provider::Custom:
            // Empty config - must be set manually
            break;
        }

        return config;
    }
};

#endif // OAUTHCONFIGFACTORY_HPP
