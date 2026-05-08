#ifndef AUTHPAGERENDERER_HPP
#define AUTHPAGERENDERER_HPP

#include <QString>

/**
 * @brief Renders OAuth callback HTML pages from Qt resources.
 * 
 * Loads HTML templates and CSS from embedded resources, combines them,
 * and substitutes placeholders with actual values.
 */
class AuthPageRenderer
{
public:
    /**
     * @brief Renders the success page shown after successful OAuth authentication.
     * @return Complete HTML page as a string.
     */
    static QString renderSuccessPage();
    
    /**
     * @brief Renders the error page shown when OAuth authentication fails.
     * @param errorMessage The error message to display to the user.
     * @return Complete HTML page as a string.
     */
    static QString renderErrorPage(const QString& errorMessage);
    
    /**
     * @brief Renders the missing authorization code error page.
     * @return Complete HTML page as a string.
     */
    static QString renderMissingCodePage();

private:
    AuthPageRenderer() = delete;  // Static utility class
    
    /**
     * @brief Loads a resource file as QString.
     * @param resourcePath Qt resource path (e.g., ":/templates/oauth/success.html")
     * @return File contents or empty string if not found.
     */
    static QString loadResource(const QString& resourcePath);
    
    /**
     * @brief Combines HTML template with CSS by replacing {{CSS}} placeholder.
     * @param html HTML template with {{CSS}} placeholder.
     * @param css CSS content to embed.
     * @return HTML with embedded CSS in <style> tag.
     */
    static QString embedCss(const QString& html, const QString& css);

    static QString getFallbackSuccessHtml()
    {
        return QStringLiteral(
            "<!DOCTYPE html><html><head><title>Success</title></head>"
            "<body style='font-family:sans-serif;text-align:center;padding:50px;'>"
            "<h1 style='color:green;'>Authentication Successful</h1>"
            "<p>You can close this window now.</p>"
            "</body></html>"
            );
    }

    static QString getFallbackErrorHtml()
    {
        return QStringLiteral(
            "<!DOCTYPE html><html><head><title>Error</title></head>"
            "<body style='font-family:sans-serif;text-align:center;padding:50px;'>"
            "<h1 style='color:red;'>Authentication Failed</h1>"
            "<p>%1</p>"
            "<p style='color:gray;'>You can close this window.</p>"
            "</body></html>"
            );
    }
};

#endif // AUTHPAGERENDERER_HPP
