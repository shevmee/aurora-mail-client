#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QButtonGroup>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTimer>
#include <QVector>

// Core modules
#include "AI/AIService.hpp"
#include "Auth/AccountRegistry.hpp"
#include "Auth/OAuthManager.hpp"
#include "Auth/PasswordCredentialsStorage.hpp"
#include "Email/EmailParser.hpp"
#include "Mail/Cache/TieredMessageCache.hpp"
#include "Mail/ImapSessionTypes.hpp"
#include "Mail/MailSessionService.hpp"
#include "Mail/MailSessionSignals.hpp"
#include "Utils/TextSanitizer.hpp"

// Mail client
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ImapClient.hpp"
#include "SmtpClient.hpp"

// Namespace aliases for convenience
using aurora::mail::app::ai::AIService;
using aurora::mail::app::email::EmailParser;
using aurora::mail::app::email::EmailSummary;
using aurora::mail::app::email::ParsedEmailContent;
using aurora::mail::app::utils::TextSanitizer;

QT_BEGIN_NAMESPACE
namespace Ui
{
  class MainWindow;
}
QT_END_NAMESPACE

// Forward declarations
class EmailListManager;
class SessionCoordinator;

namespace aurora::mail::ui
{
  class AIAssistantDialog;
}

/**
 * @enum EMainPagesIndex
 * @brief Represents the index of the different main pages in the UI.
 */
enum class EMainPagesIndex : uint8_t
{
  LoginPage = 0,  ///< Login page index
  MainPage = 1    ///< Main page index
};

/**
 * @enum EContentViewIndex
 * @brief Represents the index of content views in the main page.
 */
enum class EContentViewIndex : uint8_t
{
  InboxView = 0,   ///< Email inbox/list view
  ComposeView = 1  ///< Email composition view
};

/**
 * @enum ELoginMethodPage
 * @brief Index of pages inside the LoginCard's QStackedWidget.
 */
enum class ELoginMethodPage : uint8_t
{
  ChooserPage = 0,  ///< OAuth provider button + "use password" alternative
  PasswordPage = 1  ///< Email/password/server form for direct IMAP/SMTP login
};

/**
 * @enum EAuthMethod
 * @brief Tracks which auth method established the current session.
 *
 * Used on sign-out to clear the matching credential store and to decide
 * which form to pre-populate on the next login.
 */
enum class EAuthMethod : uint8_t
{
  None = 0,
  OAuth = 1,
  Password = 2
};

/**
 * @class MainWindow
 * @brief Main window class managing the UI for the Aurora Mail client.
 *
 * The MainWindow class handles OAuth authentication, email viewing (IMAP),
 * composing and sending emails (SMTP), folder navigation, and overall
 * application state management.
 */
class MainWindow : public QMainWindow
{
  Q_OBJECT

 public:
  /**
   * @brief Constructs a MainWindow instance.
   * @param parent The parent widget.
   */
  explicit MainWindow(QWidget* parent = nullptr);

  /**
   * @brief Constructs a MainWindow with shared SMTP and IMAP clients.
   * @param parent The parent widget.
   * @param smtpClient Shared pointer to an SMTP client.
   * @param imapClient Shared pointer to an IMAP client.
   * @param ioContext Reference to the io_context for async operations.
   */
  explicit MainWindow(
      QWidget* parent,
      const std::shared_ptr<aurora::mail::smtp::SmtpClient>& smtpClient,
      const std::shared_ptr<aurora::mail::imap::ImapClient>& imapClient,
      boost::asio::io_context& ioContext);

  /**
   * @brief Destructor.
   */
  ~MainWindow() override;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  // Authentication
  void onLogInButtonClicked();
  void onSignOutButtonClicked();
  void onOAuthAuthenticated();
  void onOAuthFailed(const QString& error);

  // Password login (alternative path when OAuth is unavailable)
  void onUsePasswordLoginClicked();
  void onBackToOAuthClicked();
  void onPasswordSignInClicked();

  // Multi-account
  void onAddAccountSelected();
  void onSwitchAccountSelected(const QString& email);

  // Navigation
  void onComposeButtonClicked();
  void onInboxButtonClicked();
  void onSentButtonClicked();
  void onDraftsButtonClicked();
  void onTrashButtonClicked();
  void onFolderButtonClicked(const QString& folderName);
  void onSidebarToggleClicked();

  // Email List
  void onEmailItemClicked(const QString& uid);
  void onRefreshButtonClicked();
  void onSearchTextChanged(const QString& text);
  void onLoadMoreClicked();
  void onMailListScrollValueChanged(int value);

  // Email Actions
  void onReplyButtonClicked();
  void onDeleteButtonClicked();
  void onMoveButtonClicked();
  void onToggleReadClicked();
  void onToggleStarClicked();

  // Compose
  void onSendButtonClicked();
  void onAttachButtonClicked();
  void onCloseComposeButtonClicked();
  void onAIAssistButtonClicked();
  void onSettingsButtonClicked();

 signals:
  /// Fired when the IDLE coroutine terminates (used by stopPolling).
  void idleLoopStopped();

 private:
  // =========================================================================
  // Initialization
  // =========================================================================

  void loadStylesheet();
  /** macOS 15 + Qt 6.9: PointingHand maps to bundled cursors that crash in ImageIO during resize. */
  void applyMacSafeCursors();
  void setupConnections();
  void initializeLoginPage();
  void initializeMainPage();
  void setupNavigationButtons();
  void setupInboxSplitter();
  void setupEmailReaderSplitter();

  // =========================================================================
  // Authentication (OAuth)
  // =========================================================================

  /**
   * @brief Initiates OAuth authentication flow.
   */
  void startOAuthFlow();

  /**
   * @brief Authenticates with mail servers using OAuth token (async on io_context; completion on Qt thread).
   */
  void authenticateWithOAuthAsync(const std::string& accessToken);

  /**
   * @brief Authenticates with mail servers using IMAP AUTHENTICATE PLAIN / SMTP AUTH PLAIN.
   *
   * Runs on io_context; completion is dispatched onto the Qt thread.
   * The password is captured by value and lives only for the duration of the
   * coroutine; on success it is persisted via PasswordCredentialsStorage iff
   * secure storage is available.
   */
  void authenticateWithPasswordAsync(PasswordCredentialsStorage::Credentials creds);

  void completeMailAuthenticationUi(bool success);

  /**
   * @brief Switches the LoginCard between chooser and password form.
   * Also clears any transient status messages and sensitive fields.
   */
  void showLoginMethodPage(ELoginMethodPage page);

  // =========================================================================
  // Multi-account management
  // =========================================================================

  /**
   * @brief Rebuilds the account-switcher popup menu from the registry.
   *
   * Thin shim that delegates to @ref SessionCoordinator::populateAccountMenu;
   * kept on @c MainWindow because it must be invocable as a Qt slot bound to
   * the menu's @c aboutToShow signal.
   */
  void rebuildAccountMenu();

  /**
   * @brief Tears down the live IMAP/SMTP session and resets UI mailbox state.
   *
   * Used when switching accounts or when the user explicitly signs out so
   * that the next session never observes data from the previous one.
   * Identity (current user, auth method, in-memory tokens) is reset by the
   * @ref SessionCoordinator at the end of this method.
   */
  void teardownActiveSession();

  void applyFolderList(std::vector<aurora::mail::imap::MailboxInfo> folders);

  // =========================================================================
  // Email Operations (IMAP)
  // =========================================================================

  /// List folders on IMAP; optional callback runs on the Qt thread after the UI list is applied.
  /// Use this to chain work that must not overlap other IMAP commands on the same connection.
  void loadFolders(std::function<void()> afterAppliedOnQtThread = {});
  void selectMailbox(const QString& mailbox);
  void loadEmails(bool append = false);
  void loadMoreEmails();
  void loadEmailContent(const QString& uid);
  void markEmailAsRead(const QString& uid, bool read = true);
  void deleteEmail(const QString& uid);
  void moveEmail(const QString& uid, const QString& destinationMailbox);
  void toggleEmailFlag(const QString& uid, const QString& flag);

  // =========================================================================
  // Email Sending (SMTP)
  // =========================================================================

  bool validateEmails(const QString& emails);
  bool isValidEmail(const QString& email);
  void clearComposeFields();
  void selectAttachments();
  void displayAttachments();
  void saveAttachment(int index);

  /**
   * @brief Resets the email reader pane to the empty "no message selected" state.
   *
   * Clears subject/meta/body text, drops the cached displayed-email key, empties
   * the attachments model and removes any attachment buttons from the UI. Use
   * this anywhere we transition the reader back to its placeholder state
   * (mailbox switch, sign-out, after delete/move, etc.) so stale attachments
   * from a previously viewed message never linger on screen.
   *
   * @param placeholder Text to show in the subject label (e.g. the localized
   *        "Select an email to view" prompt or a "Loading..." indicator).
   */
  void resetEmailReader(const QString& placeholder);

  // IDLE management
  void startPolling();
  void stopPolling();
  void checkForNewEmails();

  /**
   * @brief Resumes IDLE mode after commands complete.
   *
   * Call after IMAP commands have finished to allow IDLE to restart.
   */
  void resumeIdle();

  // =========================================================================
  // UI Helpers
  // =========================================================================

  void showStatus(const QString& message, int timeout = 3000);
  void showError(const QString& title, const QString& message);
  void setUIEnabled(bool enabled);
  void selectNavButton(QPushButton* button);
  QPushButton* createFolderButton(const QString& folderName, const QString& displayName = QString());

 private:
  std::unique_ptr<Ui::MainWindow> ui;

  // Client instances
  std::weak_ptr<aurora::mail::smtp::SmtpClient> m_smtpClient;
  std::weak_ptr<aurora::mail::imap::ImapClient> m_imapClient;
  boost::asio::io_context* m_ioContext = nullptr;

  // OAuth manager
  std::unique_ptr<OAuthManager> m_oauthManager;

  // Password credentials store (keychain-backed when available)
  PasswordCredentialsStorage m_passwordCredentials;

  // Non-secret registry of all known accounts on this install.
  AccountRegistry m_accountRegistry;

  /**
   * @brief Façade for sign-in / multi-account / sign-out scenarios.
   *
   * Concentrates the orchestration that used to live as inline slot bodies
   * in this window; instantiated after the auth dependencies above so it
   * can bind references to them.
   */
  std::unique_ptr<SessionCoordinator> m_session;

  // Popup menu attached to the sidebar account button.
  QMenu* m_accountMenu = nullptr;

  // Tracks how the current session was established (used at sign-out).
  EAuthMethod m_currentAuthMethod = EAuthMethod::None;

  // AI service for grammar checking
  std::unique_ptr<AIService> m_aiService;

  // User state
  QString m_currentUser;

  // Server configuration (could be auto-detected from email domain)
  QString m_imapServer{ "imap.gmail.com" };
  int m_imapPort{ 993 };
  QString m_smtpServer{ "smtp.gmail.com" };
  int m_smtpPort{ 587 };

  // Mailbox state
  QString m_currentMailbox{ "Inbox" };
  /// UIDVALIDITY for (m_currentUser, m_currentMailbox); 0 until first SELECT completes.
  /// All MessageKey values built by emailMessageKey() embed this so a server-side
  /// UIDVALIDITY change cannot cause us to serve a stale body for a re-used UID.
  quint32 m_currentMailboxUidValidity = 0;
  /// Set when the reader successfully shows a message; avoids redundant UID FETCH for same item.
  aurora::mail::app::cache::MessageKey m_displayedMessageKey;
  /// In-flight load coordination is delegated to the cache (reservePending/releasePending).
  /// Tier 1 (memory) + tier 2 (per-account encrypted SQLite) message body cache.
  aurora::mail::app::cache::TieredMessageCache m_messageCache;
  std::unique_ptr<MailSessionSignals> m_mailSessionSignals;
  ImapSessionLinkState m_imapLinkState{ ImapSessionLinkState::Disconnected };
  QString m_replyToMessageId;  ///< Message-ID of email being replied to (set during reply)
  EmailListManager* m_emailListManager = nullptr;

  /** After auto load-more at list bottom, require scrolling up before loading again. */
  bool m_mailListScrollReadyForMore = true;

  // Pagination
  static constexpr int EMAILS_PER_PAGE = 50;

  // Attachments (compose)
  QVector<QString> m_selectedAttachments;

  // Attachments (viewing)
  QVector<aurora::mail::app::email::AttachmentInfo> m_currentEmailAttachments;

  // Navigation
  QButtonGroup* m_navButtonGroup = nullptr;
  QList<QPushButton*> m_folderButtons;

  // UI state
  QSplitter* m_inboxSplitter = nullptr;
  QSplitter* m_emailReaderSplitter = nullptr;
  QTimer* m_sidebarHideTimer = nullptr;

  /// IMAP IDLE + pump + queue; owns strand internally — see MailSessionService.
  std::unique_ptr<MailSessionService> m_mailSession;

  /// Enqueue an operation (coalescing); schedules processing on io_context (non-blocking on Qt).
  void enqueueImapOperation(ImapOperation op);

  boost::asio::awaitable<void> dispatchImapOperation(const ImapOperation& op);
  boost::asio::awaitable<void> fetchMailboxPageAwaitable(bool append, int serverMessageCountOrNeg1);

  void applyParsedEmailListOnQt(const QVector<EmailSummary>& emails, bool append, int startSeq);
  void applyParsedEmailBodyOnQt(const ParsedEmailContent& content, const QString& uid);

  void handleIdleServerNotification(const QString& notification);

  /** Gmail default IMAP path for Sent (same as Sent nav button). */
  [[nodiscard]] static QString gmailSentMailboxPath();

  [[nodiscard]] ImapSessionCallbacks buildImapSessionCallbacks();

  /**
   * Build a strongly-typed MessageKey for (m_currentUser, m_currentMailbox, uid).
   *
   * Embeds m_currentMailboxUidValidity so a server-side UIDVALIDITY change cannot
   * silently re-use the cached body of an old UID for a freshly-issued one.
   */
  [[nodiscard]] aurora::mail::app::cache::MessageKey emailMessageKey(const QString& uid) const
  {
    aurora::mail::app::cache::MessageKey key;
    key.accountId = m_currentUser;
    key.mailbox = m_currentMailbox;
    key.uidValidity = m_currentMailboxUidValidity;
    key.uid = uid.toUInt();
    return key;
  }

  /**
   * Ensure a per-account persistent (encrypted SQLite) tier is attached for
   * `m_currentUser`. Idempotent. Safe to call from the Qt thread; runs the
   * one-shot keychain handshake required to derive the AES-256-GCM master key.
   *
   * If the keychain backend is not available, no persistent tier is attached
   * and the cache silently degrades to memory-only.
   */
  void ensureMessageCacheForCurrentAccount();

  void publishImapLinkState(ImapSessionLinkState state);

  // Regex patterns
  static QRegularExpression s_emailRegex;
  static QRegularExpression s_splitRegex;
};

#endif  // MAINWINDOW_HPP
