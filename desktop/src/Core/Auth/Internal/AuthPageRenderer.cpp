#include "AuthPageRenderer.hpp"

#include <QFile>
#include <QDebug>

namespace {
    const QString SUCCESS_HTML_PATH = QStringLiteral(":/templates/oauth/success.html");
    const QString SUCCESS_CSS_PATH = QStringLiteral(":/templates/oauth/success.css");
    const QString ERROR_HTML_PATH = QStringLiteral(":/templates/oauth/error.html");
    const QString ERROR_CSS_PATH = QStringLiteral(":/templates/oauth/error.css");
    
    const QString CSS_PLACEHOLDER = QStringLiteral("{{CSS}}");
    const QString ERROR_MESSAGE_PLACEHOLDER = QStringLiteral("{{ERROR_MESSAGE}}");
}

QString AuthPageRenderer::loadResource(const QString& resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to load resource:" << resourcePath << file.errorString();
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString AuthPageRenderer::embedCss(const QString& html, const QString& css)
{
    QString result = html;
    result.replace(CSS_PLACEHOLDER, css);
    return result;
}

QString AuthPageRenderer::renderSuccessPage()
{
    QString html = loadResource(SUCCESS_HTML_PATH);
    QString css = loadResource(SUCCESS_CSS_PATH);
    
    if (html.isEmpty()) {
        // Fallback if resources not found
        return getFallbackSuccessHtml();
    }
    
    return embedCss(html, css);
}

QString AuthPageRenderer::renderErrorPage(const QString& errorMessage)
{
    QString html = loadResource(ERROR_HTML_PATH);
    QString css = loadResource(ERROR_CSS_PATH);
    
    if (html.isEmpty()) {
        // Fallback if resources not found
        return getFallbackErrorHtml().arg(errorMessage.toHtmlEscaped());
    }
    
    QString result = embedCss(html, css);
    result.replace(ERROR_MESSAGE_PLACEHOLDER, errorMessage.toHtmlEscaped());
    return result;
}

QString AuthPageRenderer::renderMissingCodePage()
{
    return renderErrorPage(QStringLiteral("Missing authorization code in the response."));
}
