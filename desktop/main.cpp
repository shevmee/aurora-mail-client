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
#include <openssl/x509.h>

#ifdef Q_OS_WIN
#  include <openssl/err.h>
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <wincrypt.h>
#  include <dbghelp.h>
#  include <psapi.h>
#endif

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

#ifdef Q_OS_WIN
/**
 * @brief Top-level Windows SEH filter that diagnoses hard crashes (AV, etc.).
 *
 * The default behaviour for an unhandled structured exception in a
 * WIN32_EXECUTABLE app is "process disappears, $LASTEXITCODE = 0xC0000005,
 * no log, no message box". For a desktop user that is indistinguishable
 * from `exit(0)`. This filter:
 *   1. logs the exception code and the offending instruction address;
 *   2. resolves the address to a module + offset via PSAPI so we know
 *      whether it crashed in our code, in Qt, in OpenSSL, or in asio;
 *   3. writes a minidump to %TEMP%\aurora-mail-crash-<pid>.dmp that can
 *      be opened in WinDbg / Visual Studio for a full stack trace.
 *
 * We deliberately return EXCEPTION_EXECUTE_HANDLER after writing the dump
 * so the process exits cleanly instead of letting Windows pop the generic
 * "Aurora Mail has stopped working" dialog.
 */
LONG WINAPI auroraCrashFilter(EXCEPTION_POINTERS* info)
{
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const DWORD code = info->ExceptionRecord->ExceptionCode;
    void* const addr = info->ExceptionRecord->ExceptionAddress;

    // Resolve the offending address to <module>+<offset> to give us a
    // fighting chance of debugging without a debugger attached.
    char moduleName[MAX_PATH] = "<unknown>";
    uintptr_t offset = 0;
    HMODULE hMod = nullptr;
    if (::GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(addr),
            &hMod) != 0)
    {
        ::GetModuleFileNameA(hMod, moduleName, sizeof(moduleName));
        offset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);
    }

    qCritical().nospace()
        << "Aurora crashed: SEH code=0x" << QString::number(code, 16)
        << " addr=0x" << QString::number(reinterpret_cast<quintptr>(addr), 16)
        << " in " << moduleName
        << "+0x" << QString::number(offset, 16);

    // Write a minidump for post-mortem analysis. Failure here is non-fatal
    // — we still want to surface the crash text above.
    char dumpPath[MAX_PATH] = {};
    DWORD tempLen = ::GetTempPathA(sizeof(dumpPath), dumpPath);
    if (tempLen > 0 && tempLen < sizeof(dumpPath) - 64) {
        std::snprintf(
            dumpPath + tempLen,
            sizeof(dumpPath) - tempLen,
            "aurora-mail-crash-%lu.dmp",
            static_cast<unsigned long>(::GetCurrentProcessId()));

        HANDLE hFile = ::CreateFileA(
            dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei{};
            mdei.ThreadId = ::GetCurrentThreadId();
            mdei.ExceptionPointers = info;
            mdei.ClientPointers = FALSE;

            const BOOL ok = ::MiniDumpWriteDump(
                ::GetCurrentProcess(),
                ::GetCurrentProcessId(),
                hFile,
                MiniDumpWithDataSegs,
                &mdei,
                nullptr,
                nullptr);
            ::CloseHandle(hFile);
            if (ok != FALSE) {
                qCritical() << "Minidump written to" << dumpPath;
            } else {
                qWarning() << "MiniDumpWriteDump failed for" << dumpPath
                           << ", GetLastError=" << ::GetLastError();
            }
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * @brief Populate an OpenSSL trust store from the Windows Certificate Store.
 *
 * On macOS and Linux OpenSSL has well-known places (homebrew/openssl@3,
 * /etc/ssl/certs, …) and ssl::context::set_default_verify_paths() is enough.
 * On Windows OpenSSL ships *no* CA bundle, so set_default_verify_paths()
 * succeeds silently and every TLS handshake then fails with
 * "certificate verify failed (unable to get local issuer certificate)".
 *
 * The portable fix is to import the system trust roots into OpenSSL's
 * X509_STORE on startup. We pull from "ROOT" (trusted CAs) and "CA"
 * (intermediate CAs cached by Windows for chain building); the latter
 * helps when servers don't send a complete chain. Both stores are read
 * via the well-tested CryptoAPI; we never write to them.
 *
 * This avoids the alternative of shipping a static cert.pem (which would
 * need rotation) or pinning a single CA (which would break for any other
 * mail provider the user adds later).
 */
void loadWindowsRootCertificates(ssl::context& ctx)
{
    X509_STORE* store = SSL_CTX_get_cert_store(ctx.native_handle());
    if (store == nullptr) {
        qWarning() << "TLS: SSL_CTX_get_cert_store returned null; cannot import Windows roots";
        return;
    }

    int importedCount = 0;
    for (const char* storeName : { "ROOT", "CA" }) {
        HCERTSTORE hSysStore = ::CertOpenSystemStoreA(0, storeName);
        if (hSysStore == nullptr) {
            qWarning() << "TLS: CertOpenSystemStoreA failed for store" << storeName;
            continue;
        }

        PCCERT_CONTEXT pContext = nullptr;
        while ((pContext = ::CertEnumCertificatesInStore(hSysStore, pContext)) != nullptr) {
            // pbCertEncoded is DER (BLOB); d2i_X509 advances the pointer it is given,
            // so feed it a local copy to leave the original BLOB untouched.
            const unsigned char* encoded = pContext->pbCertEncoded;
            X509* x509 = ::d2i_X509(nullptr, &encoded, static_cast<long>(pContext->cbCertEncoded));
            if (x509 == nullptr) {
                // Skip malformed certs silently – the Windows store occasionally
                // contains test/legacy entries that OpenSSL refuses to parse.
                ::ERR_clear_error();
                continue;
            }
            // X509_STORE_add_cert returns 0 on duplicates (since the previous
            // store name) – that is expected and not an error for us.
            if (::X509_STORE_add_cert(store, x509) == 1) {
                ++importedCount;
            } else {
                ::ERR_clear_error();
            }
            ::X509_free(x509);
        }
        ::CertCloseStore(hSysStore, 0);
    }

    if (importedCount == 0) {
        qWarning() << "TLS: imported 0 root certificates from Windows store; TLS will fail";
    } else {
        qDebug() << "TLS: imported" << importedCount << "root certificates from Windows store";
    }
}
#endif  // Q_OS_WIN

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

#ifdef Q_OS_WIN
    // OpenSSL on Windows ships no CA bundle, so set_default_verify_paths()
    // is a no-op and every TLS handshake fails. Pull system roots from
    // CryptoAPI to make verification actually succeed for IMAPS/SMTPS.
    loadWindowsRootCertificates(ctx);
#endif

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
#ifdef Q_OS_WIN
    // Install before anything else: the SEH filter must be in place before
    // any code path that might dereference a stale pointer (asio thread,
    // Qt event loop, etc.). Otherwise hard crashes are silent and produce
    // only an opaque $LASTEXITCODE = 0xC0000005.
    ::SetUnhandledExceptionFilter(auroraCrashFilter);
#endif

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
