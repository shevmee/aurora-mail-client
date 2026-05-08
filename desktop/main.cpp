#include <QApplication>
#include <QFontDatabase>
#include <QFont>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QStyle>
#include <QStyleFactory>
#include <QMessageBox>
#include <QStandardPaths>
#include <QEvent>
#include <QTranslator>

#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>

#include "Ui/MainWindow.hpp"
#include "IoContext/IoContextRunner.hpp"

#include "SmtpClient.hpp"
#include "ImapClient.hpp"
#include "LoggerInstance.hpp"
#include "Config/AppConfig.hpp"

#ifdef Q_OS_MACOS
#include "MacNativeCursorFilter.hpp"
#endif

namespace ssl = boost::asio::ssl;
using namespace aurora::mail::common;

namespace {

/**
 * @brief Gets the application data directory, creating it if needed.
 * 
 * Uses QStandardPaths::AppDataLocation which maps to:
 * - macOS: ~/Library/Application Support/Aurora/
 * - Linux: ~/.local/share/Aurora/
 * - Windows: C:/Users/<USER>/AppData/Local/Aurora/
 */
QString getAppDataDir()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dataDir;
}

/**
 * @brief Finds the config file path.
 * 
 * Returns: <AppDataLocation>/config.json
 * Creates directory if it doesn't exist.
 */
QString findConfigPath()
{
    return getAppDataDir() + "/config.json";
}

/**
 * @brief Copies default config to app data dir if not present.
 */
bool ensureConfigExists()
{
    QString configPath = findConfigPath();
    
    if (QFile::exists(configPath)) {
        return true;
    }
    
    // Look for bundled default config
    QString defaultConfig;
    QString appDir = QCoreApplication::applicationDirPath();
    
#ifdef Q_OS_MACOS
    // Check bundle Resources
    defaultConfig = appDir + "/../Resources/config.json";
#else
    // Check next to executable
    defaultConfig = appDir + "/config.json";
#endif
    
    if (QFile::exists(defaultConfig)) {
        return QFile::copy(defaultConfig, configPath);
    }
    
    // No default config found
    return false;
}

/**
 * @brief Configures SSL context for secure mail connections.
 *
 * Policy: TLS 1.3 preferred, with fallback to TLS 1.2 for servers that don't
 * advertise 1.3 (still common for some IMAP/SMTP submission endpoints).
 * This matches NFR-04 and the design described in section 2.5.1 of the thesis.
 */
void configureSslContext(ssl::context& ctx)
{
    ctx.set_verify_mode(ssl::verify_peer);
    ctx.set_default_verify_paths();
    ctx.set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::no_sslv3 |
        ssl::context::no_tlsv1 |
        ssl::context::no_tlsv1_1 |
        ssl::context::single_dh_use
    );
    // Bound the negotiated version explicitly: enable TLS 1.2 and TLS 1.3
    // (and only these). On OpenSSL 3.x this is the supported way to express
    // the policy without being tied to a single TLS_method-style constructor.
    SSL_CTX_set_min_proto_version(ctx.native_handle(), TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx.native_handle(), TLS1_3_VERSION);
}

/**
 * @brief Configures platform-specific settings before QApplication creation.
 */
void configurePlatformSettings()
{
#ifdef Q_OS_MACOS
    // Qt 6.9 + macOS 15: AppKit can crash in setCursorFromBundle→ImageIO when showing "hand" cursors
    // during resize. Fusion alone is not enough; avoid PointingHand in the UI (see applyMacSafeCursors).
    qputenv("QT_STYLE_OVERRIDE", "Fusion");
#endif
}

/**
 * @brief Configures font with emoji fallback support.
 */
#ifdef Q_OS_MACOS
/**
 * Qt may leave SplitH/SplitV cursors on AppKit's override stack; AppKit then crashes in
 * setCursorFromBundle→ImageIO when resizing the window. Clear the stack before mouse handling.
 */
class AuroraApplication final : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override
    {
        if (event && event->type() == QEvent::MouseButtonPress) {
            while (QApplication::overrideCursor()) {
                QApplication::restoreOverrideCursor();
            }
        }
        return QApplication::notify(receiver, event);
    }
};
#endif

void configureFont(QApplication& app)
{
    QFont defaultFont = app.font();
    QStringList families = {defaultFont.family()};
    
#ifdef Q_OS_MACOS
    families << "Apple Color Emoji";
#elif defined(Q_OS_WIN)
    families << "Segoe UI Emoji" << "Segoe UI Symbol";
#else
    families << "Noto Color Emoji" << "Symbola";
#endif
    
    defaultFont.setFamilies(families);
    app.setFont(defaultFont);
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    configurePlatformSettings(); // Must be called before QApplication

#ifdef Q_OS_MACOS
    AuroraApplication app(argc, argv);
    installMacNativeCursorFilter();
#else
    QApplication app(argc, argv);
#endif

#ifdef Q_OS_MACOS
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }
#endif
    
    // Set application metadata
    app.setApplicationName("Aurora Mail");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Aurora");
    
    configureFont(app);
    
    // Ensure config exists (copy from bundle if first run)
    QString configPath = findConfigPath();
    
    if (!ensureConfigExists()) {
        QMessageBox::warning(nullptr, "Configuration",
            QString("No config file found. Creating default at:\n%1\n\n"
                    "Please edit this file with your settings.")
                .arg(configPath));
    }
    
    // Load desktop application configuration (timeout, logger, locale).
    auto configResult = aurora::mail::app::config::loadAppConfig(configPath);
    
    if (!configResult) {
        QMessageBox::critical(nullptr, "Configuration Error",
            QString("Failed to load config from:\n%1\n\nError: %2\n\n"
                    "Please ensure the file exists and is valid JSON.")
                .arg(configPath)
                .arg(QString::fromStdString(configResult.error())));
        return 1;
    }
    
    const aurora::mail::app::config::AppConfig& config = *configResult;
    
    // Install translators *before* building any UI: anything constructed after
    // this point will resolve tr() through the installed QTranslator.
    // Order: Qt's own translations first (so QDialogButtonBox etc. are
    // localized), then the application catalog (NFR-10).
    static QTranslator qtBaseTranslator;
    static QTranslator appTranslator;
    QLocale uiLocale = config.locale.empty()
        ? QLocale::system()
        : QLocale(QString::fromStdString(config.locale));
    QLocale::setDefault(uiLocale);

    if (qtBaseTranslator.load(uiLocale, "qtbase", "_",
        QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        app.installTranslator(&qtBaseTranslator);
    }
    if (appTranslator.load(uiLocale, "aurora-mail", "_", ":/translations"))
    {
        app.installTranslator(&appTranslator);
    }
    
    // Initialize logger from config
    logger::LoggerInstance::instance().init(config.logger);
    
    // Initialize async I/O
    IoContextRunner ioRunner;
    
    // Configure SSL context: any TLS version, narrowed to 1.2/1.3 by configureSslContext.
    ssl::context sslContext(ssl::context::tls_client);
    configureSslContext(sslContext);
    
    // Create mail clients with configured timeout
    auto smtpClient = std::make_shared<aurora::mail::smtp::SmtpClient>(
        ioRunner.get(),
        sslContext,
        config.timeoutSeconds
    );
    
    auto imapClient = std::make_shared<aurora::mail::imap::ImapClient>(
        ioRunner.get(),
        sslContext,
        config.timeoutSeconds
    );
    
    // Create and show main window
    MainWindow window(nullptr, smtpClient, imapClient, ioRunner.get());
    window.show();
    
    return app.exec();
}
