#include "MainWindow.hpp"

#include "./ui_MainWindow.h"
#include "Coordinators/SessionCoordinator.hpp"
#include "EmailListManager.hpp"
#include "Mail/Cache/CacheKeyMaterial.hpp"
#include "Mail/Cache/CachedMessage.hpp"
#include "Widgets/AIAssistantDialog.hpp"
#include "Widgets/AiSettingsDialog.hpp"

// Qt includes
#include <QAbstractSlider>
#include <QAction>
#include <QDebug>
#include <QDesktopServices>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSizePolicy>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#ifdef Q_OS_MACOS
#include <QSplitterHandle>
#endif

// Mail client includes
#include "ImapCommand.hpp"
#include "MailAddress.hpp"
#include "MailAttachment.hpp"
#include "MailMessageBuilder.hpp"
#include "SmtpCommand.hpp"

// Boost includes
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstddef>
#include <vector>

// Standard library
#include <algorithm>
#include <optional>

// Core types
using aurora::mail::app::email::AttachmentInfo;
using aurora::mail::app::email::ParsedEmailContent;

// OAuth configuration
#include "OAuthCredentials.hpp"
using aurora::mail::app::config::OAuthCredentials;

// Static regex patterns
QRegularExpression MainWindow::s_emailRegex("^[\\w\\.-]+@[\\w\\.-]+\\.\\w+$");
QRegularExpression MainWindow::s_splitRegex("\\s*,\\s*|\\s+");

namespace
{
#ifdef Q_OS_MACOS
  constexpr Qt::CursorShape kInteractiveCursorShape = Qt::ArrowCursor;

  void setMacSplitterHandlesArrow(QSplitter* splitter)
  {
    if (splitter == nullptr)
    {
      return;
    }
    for (int i = 0; i < splitter->count() - 1; ++i)
    {
      if (QSplitterHandle* h = splitter->handle(i))
      {
        h->setCursor(Qt::ArrowCursor);
      }
    }
  }
#else
  constexpr Qt::CursorShape kInteractiveCursorShape = Qt::PointingHandCursor;
#endif
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui(std::make_unique<Ui::MainWindow>()),
      m_oauthManager(std::make_unique<OAuthManager>(Provider::Gmail, this)),
      m_aiService(std::make_unique<AIService>(this)),
      m_mailSessionSignals(std::make_unique<MailSessionSignals>(this))
{
  ui->setupUi(this);
  ui->mailListColumnLayout->setStretch(0, 0);
  ui->mailListColumnLayout->setStretch(1, 1);
  m_emailListManager = new EmailListManager(ui->mailItemsLayout, ui->MailListContainer, ui->EmailActionsBar, this);
  loadStylesheet();
  applyMacSafeCursors();
  setupConnections();
  initializeLoginPage();
  setupInboxSplitter();
  setupEmailReaderSplitter();

  // Hide action buttons bar until an email is selected
  ui->EmailActionsBar->hide();
}

MainWindow::MainWindow(
    QWidget* parent,
    const std::shared_ptr<aurora::mail::smtp::SmtpClient>& smtpClient,
    const std::shared_ptr<aurora::mail::imap::ImapClient>& imapClient,
    boost::asio::io_context& ioContext)
    : QMainWindow(parent),
      ui(std::make_unique<Ui::MainWindow>()),
      m_smtpClient(smtpClient),
      m_imapClient(imapClient),
      m_ioContext(&ioContext),
      m_oauthManager(std::make_unique<OAuthManager>(Provider::Gmail, this)),
      m_aiService(std::make_unique<AIService>(this)),
      m_mailSessionSignals(std::make_unique<MailSessionSignals>(this))
{
  ui->setupUi(this);
  ui->mailListColumnLayout->setStretch(0, 0);
  ui->mailListColumnLayout->setStretch(1, 1);
  m_emailListManager = new EmailListManager(ui->mailItemsLayout, ui->MailListContainer, ui->EmailActionsBar, this);
  loadStylesheet();
  applyMacSafeCursors();
  setupConnections();
  initializeLoginPage();
  setupInboxSplitter();
  setupEmailReaderSplitter();

  m_mailSession = std::make_unique<MailSessionService>(
      ioContext,
      imapClient,
      buildImapSessionCallbacks(),
      [this](const ImapOperation& op) -> boost::asio::awaitable<void> { return dispatchImapOperation(op); });

  // Hide action buttons bar until an email is selected
  ui->EmailActionsBar->hide();
}

MainWindow::~MainWindow()
{
  stopPolling();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
  // No special event handling needed for now
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::loadStylesheet()
{
  QFile styleFile(":/styles/styles.qss");
  if (!styleFile.exists())
  {
    styleFile.setFileName("Resources/styles.qss");
  }

  if (styleFile.open(QFile::ReadOnly | QFile::Text))
  {
    QTextStream stream(&styleFile);
    setStyleSheet(stream.readAll());
    styleFile.close();
  }
  else
  {
    qWarning() << "Could not load stylesheet";
  }
}

void MainWindow::applyMacSafeCursors()
{
#ifdef Q_OS_MACOS
  // Qt 6.9 + macOS 15: PointingHand / SplitH / SplitV map to AppKit bundled
  // cursors that occasionally fail to load via setCursorFromBundle → ImageIO,
  // crashing the process during a window resize. Forcing ArrowCursor on every
  // widget that would otherwise request one of those shapes keeps Qt from
  // ever entering the bundle-loading code path. This is the single, narrowly
  // scoped place where the workaround lives — no QApplication subclassing,
  // no AppKit method swizzling, no global event monitors.
  const auto isUnsafeShape = [](Qt::CursorShape s) noexcept {
    return s == Qt::PointingHandCursor
        || s == Qt::SplitHCursor
        || s == Qt::SplitVCursor;
  };

  for (QWidget* w : findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively))
  {
    if (isUnsafeShape(w->cursor().shape()))
    {
      w->setCursor(Qt::ArrowCursor);
    }
  }
  if (isUnsafeShape(cursor().shape()))
  {
    setCursor(Qt::ArrowCursor);
  }

  // QSplitter installs SplitH/SplitV cursors on its handles automatically;
  // route through the existing helper, which is also reused for splitters
  // that are constructed later (see setupInboxSplitter / setupEmailReaderSplitter).
  for (QSplitter* splitter : findChildren<QSplitter*>())
  {
    setMacSplitterHandlesArrow(splitter);
  }
#endif
}

void MainWindow::setupConnections()
{
  // ------------------------------------------------------------------
  // Session / multi-account façade.
  // Constructed once we have all of its bindings ready (OAuthManager,
  // PasswordCredentialsStorage, AccountRegistry and identity-state members
  // are all already initialised by this point — they live on this window).
  // ------------------------------------------------------------------
  m_session = std::make_unique<SessionCoordinator>(
      SessionCoordinator::Bindings{
          *m_oauthManager,
          m_passwordCredentials,
          m_accountRegistry,
          m_currentUser,
          m_currentAuthMethod,
          m_imapServer,
          m_imapPort,
          m_smtpServer,
          m_smtpPort,
      },
      this);

  // UI side-effects emitted by the coordinator. Kept compact: a status-bar
  // label setter, the standard UI-enable toggle, and an error dialog.
  connect(m_session.get(), &SessionCoordinator::statusMessageRequested, ui->StatusLabel, &QLabel::setText);
  connect(m_session.get(), &SessionCoordinator::uiEnableRequested, this, &MainWindow::setUIEnabled);
  connect(m_session.get(), &SessionCoordinator::errorMessageRequested, this, &MainWindow::showError);

  // Hand-offs back to the host's async network paths and view transitions.
  connect(m_session.get(), &SessionCoordinator::passwordSignInRequested, this, &MainWindow::authenticateWithPasswordAsync);
  // Queued so the OAuth completion runs once the current call stack
  // unwinds (matches the original QTimer::singleShot(0, ...) pattern).
  connect(
      m_session.get(),
      &SessionCoordinator::oauthCompletionRequested,
      this,
      &MainWindow::onOAuthAuthenticated,
      Qt::QueuedConnection);
  // SessionCoordinator emits sessionTeardownRequested only on explicit
  // sign-out — we therefore route it to the cache-destroying variant. The
  // soft-teardown variant (cache preserved) is invoked directly from the
  // account-switch and add-another-account paths in this view.
  connect(
      m_session.get(),
      &SessionCoordinator::sessionTeardownRequested,
      this,
      [this]() { teardownActiveSession(/*destroyPersistentCache=*/true); });
  connect(
      m_session.get(),
      &SessionCoordinator::loginPageRequested,
      this,
      [this]()
      {
        ui->MainPagesStack->setCurrentIndex(static_cast<int>(EMainPagesIndex::LoginPage));
        showLoginMethodPage(ELoginMethodPage::ChooserPage);
      });

  // Account-switcher menu actions emit on the coordinator; route them to
  // the existing slots so the wiring around them (page transitions, etc.)
  // continues to live in the view.
  connect(m_session.get(), &SessionCoordinator::switchAccountRequested, this, &MainWindow::onSwitchAccountSelected);
  connect(m_session.get(), &SessionCoordinator::addAccountRequested, this, &MainWindow::onAddAccountSelected);
  connect(m_session.get(), &SessionCoordinator::signOutRequested, this, &MainWindow::onSignOutButtonClicked);

  // Login
  connect(ui->LogInButton, &QPushButton::clicked, this, &MainWindow::onLogInButtonClicked);
  connect(ui->UsePasswordButton, &QPushButton::clicked, this, &MainWindow::onUsePasswordLoginClicked);
  connect(ui->BackToOAuthButton, &QPushButton::clicked, this, &MainWindow::onBackToOAuthClicked);
  connect(ui->PasswordSignInButton, &QPushButton::clicked, this, &MainWindow::onPasswordSignInClicked);
  // Pressing Enter in either text field submits the password login form.
  connect(ui->PasswordLine, &QLineEdit::returnPressed, this, &MainWindow::onPasswordSignInClicked);
  connect(ui->LoginLine, &QLineEdit::returnPressed, this, &MainWindow::onPasswordSignInClicked);
  connect(ui->SignOutButton, &QPushButton::clicked, this, &MainWindow::onSignOutButtonClicked);
  connect(ui->SettingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsButtonClicked);

  // OAuth signals
  connect(m_oauthManager.get(), &OAuthManager::authenticated, this, &MainWindow::onOAuthAuthenticated);
  connect(m_oauthManager.get(), &OAuthManager::authenticationFailed, this, &MainWindow::onOAuthFailed);

  // Navigation
  connect(ui->ComposeButton, &QPushButton::clicked, this, &MainWindow::onComposeButtonClicked);
  connect(ui->InboxButton, &QPushButton::clicked, this, &MainWindow::onInboxButtonClicked);
  connect(ui->SentButton, &QPushButton::clicked, this, &MainWindow::onSentButtonClicked);
  connect(ui->DraftsButton, &QPushButton::clicked, this, &MainWindow::onDraftsButtonClicked);
  connect(ui->TrashButton, &QPushButton::clicked, this, &MainWindow::onTrashButtonClicked);
  connect(ui->SidebarToggleButton, &QPushButton::clicked, this, &MainWindow::onSidebarToggleClicked);

  // Mail list
  connect(ui->RefreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshButtonClicked);
  connect(ui->SearchLine, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
  if (m_emailListManager != nullptr)
  {
    connect(m_emailListManager, &EmailListManager::emailSelected, this, &MainWindow::onEmailItemClicked);
    connect(m_emailListManager, &EmailListManager::loadMoreRequested, this, &MainWindow::onLoadMoreClicked);
  }

  connect(
      ui->MailListArea->verticalScrollBar(),
      &QAbstractSlider::valueChanged,
      this,
      &MainWindow::onMailListScrollValueChanged);

  // Email actions
  connect(ui->ReplyButton, &QPushButton::clicked, this, &MainWindow::onReplyButtonClicked);
  connect(ui->DeleteButton, &QPushButton::clicked, this, &MainWindow::onDeleteButtonClicked);
  connect(ui->MoveButton, &QPushButton::clicked, this, &MainWindow::onMoveButtonClicked);
  connect(ui->ToggleReadButton, &QPushButton::clicked, this, &MainWindow::onToggleReadClicked);
  connect(ui->StarButton, &QPushButton::clicked, this, &MainWindow::onToggleStarClicked);

  // Compose
  connect(ui->SendButton, &QPushButton::clicked, this, &MainWindow::onSendButtonClicked);
  connect(ui->AttachButton, &QPushButton::clicked, this, &MainWindow::onAttachButtonClicked);
  connect(ui->CloseComposeButton, &QPushButton::clicked, this, &MainWindow::onCloseComposeButtonClicked);
  connect(ui->AIAssistButton, &QPushButton::clicked, this, &MainWindow::onAIAssistButtonClicked);

  // Account switcher: install a popup menu on the sidebar account button.
  // The menu is rebuilt on demand so it always reflects the current
  // registry state (added/removed accounts, active highlight).
  m_accountMenu = new QMenu(this);
  if (auto* btn = qobject_cast<QToolButton*>(ui->UserEmailLabel))
  {
    btn->setMenu(m_accountMenu);
  }
  connect(m_accountMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildAccountMenu);

  setupNavigationButtons();
}

void MainWindow::initializeLoginPage()
{
  // Pre-fill last-used server hints (non-secret) for the password form.
  const auto hints = m_passwordCredentials.lastServerHints();
  ui->ImapServerLine->setText(QStringLiteral("%1:%2").arg(hints.imapServer).arg(hints.imapPort));
  ui->SmtpServerLine->setText(QStringLiteral("%1:%2").arg(hints.smtpServer).arg(hints.smtpPort));
  ui->LoginLine->setText(m_passwordCredentials.lastEmail());
  ui->PasswordLine->clear();

  // OAuth path: configure provider and gate the button accordingly.
  const bool oauthConfigured = OAuthCredentials::isConfigured();
  if (!oauthConfigured)
  {
    ui->LogInButton->setEnabled(false);
    ui->LogInButton->setToolTip(OAuthCredentials::getMissingCredentialsMessage());
    ui->LoginHintLabel->setText(QStringLiteral("OAuth is not configured. Use email and password to sign in."));
    qWarning() << OAuthCredentials::getMissingCredentialsMessage();
  }
  else
  {
    ui->LogInButton->setEnabled(true);
    ui->LogInButton->setToolTip(QString{});
    ui->LoginHintLabel->setText(QString{});
    m_oauthManager->setCredentials(OAuthCredentials::clientId(), OAuthCredentials::clientSecret());

    // === Multi-account auto-resume ===
    // Delegated to the session coordinator: it picks the right resume
    // path (registry's active account → first account → legacy single-
    // account migration) and emits a status message + the appropriate
    // OAuth/password kick-off back to this window.
    if (m_session && m_session->tryAutoResume())
    {
      return;
    }
    // If no resume path is available, fall through to the login page so
    // the user can either re-authenticate or pick a different account.
  }

  // Warn the user if password storage will silently fail.
  if (!PasswordCredentialsStorage::isSecure())
  {
    const auto current = ui->LoginHintLabel->text();
    const QString warn = QStringLiteral("Secure storage unavailable: passwords will not be remembered after restart.");
    ui->LoginHintLabel->setText(current.isEmpty() ? warn : current + QStringLiteral("\n") + warn);
  }

  ui->MainPagesStack->setCurrentIndex(static_cast<int>(EMainPagesIndex::LoginPage));
  showLoginMethodPage(ELoginMethodPage::ChooserPage);
}

void MainWindow::showLoginMethodPage(ELoginMethodPage page)
{
  ui->LoginMethodStack->setCurrentIndex(static_cast<int>(page));
  ui->StatusLabel->clear();
  if (page == ELoginMethodPage::PasswordPage)
  {
    // If we have stored creds for the last email, transparently pre-fill them.
    if (auto stored = m_passwordCredentials.load(ui->LoginLine->text()); stored.has_value())
    {
      ui->PasswordLine->setText(stored->password);
      ui->ImapServerLine->setText(QStringLiteral("%1:%2").arg(stored->imapServer).arg(stored->imapPort));
      ui->SmtpServerLine->setText(QStringLiteral("%1:%2").arg(stored->smtpServer).arg(stored->smtpPort));
    }
    ui->LoginLine->setFocus();
  }
  else
  {
    // Drop the password from the form widget when leaving the password page.
    ui->PasswordLine->clear();
  }
}

void MainWindow::initializeMainPage()
{
  ui->UserEmailLabel->setText(m_currentUser);
  ui->ContentStack->setCurrentIndex(static_cast<int>(EContentViewIndex::InboxView));

  // Ensure the reader pane starts in the empty-placeholder state on every
  // sign-in. Without this, residual attachments / body from a prior session
  // (e.g. before sign-out) could remain visible while no message is selected.
  resetEmailReader(QStringLiteral("Select an email to view"));

  // LIST must finish before SELECT/FETCH: both use one ImapClient stream; overlapping coroutines desync tags.
  loadFolders([this]() { selectMailbox("INBOX"); });

  // Note: startPolling() is called after the first loadEmails() completes
  // to avoid race conditions between FETCH and IDLE commands
}

void MainWindow::setupNavigationButtons()
{
  m_navButtonGroup = new QButtonGroup(this);
  m_navButtonGroup->setExclusive(true);

  m_navButtonGroup->addButton(ui->InboxButton);
  m_navButtonGroup->addButton(ui->SentButton);
  m_navButtonGroup->addButton(ui->DraftsButton);
  m_navButtonGroup->addButton(ui->TrashButton);
}

void MainWindow::setupInboxSplitter()
{
  // Create a splitter for the inbox view to allow resizing the mail list column
  m_inboxSplitter = new QSplitter(Qt::Horizontal, ui->InboxView);

  // Take widgets from the existing layout and add to splitter
  // The layout has: MailListColumn, MailListDivider, EmailContent

  // Remove widgets from layout (they'll be reparented to splitter)
  ui->inboxViewLayout->removeWidget(ui->MailListColumn);
  ui->inboxViewLayout->removeWidget(ui->MailListDivider);
  ui->inboxViewLayout->removeWidget(ui->EmailContent);

  // Hide the divider (splitter has its own handle)
  ui->MailListDivider->hide();

  // Add widgets to splitter
  m_inboxSplitter->addWidget(ui->MailListColumn);
  m_inboxSplitter->addWidget(ui->EmailContent);

  // Size constraints (allow wide list column; user adjusts via splitter)
  ui->MailListColumn->setMinimumWidth(220);
  ui->MailListColumn->setMaximumWidth(16777215);
  ui->EmailContent->setMinimumWidth(320);

  // Restore or set initial sizes (mail list: 350px, email content: rest)
  QSettings settings;
  if (settings.contains(QStringLiteral("ui/inbox_splitter_sizes")))
  {
    const QList<QVariant> sizesVar = settings.value(QStringLiteral("ui/inbox_splitter_sizes")).toList();
    QList<int> sz;
    sz.reserve(sizesVar.size());
    for (const QVariant& v : sizesVar)
    {
      sz.append(v.toInt());
    }
    if (sz.size() >= 2)
    {
      m_inboxSplitter->setSizes(sz);
    }
    else
    {
      m_inboxSplitter->setSizes({ 350, 600 });
    }
  }
  else
  {
    m_inboxSplitter->setSizes({ 350, 600 });
  }

  connect(
      m_inboxSplitter,
      &QSplitter::splitterMoved,
      this,
      [this](int /*pos*/, int /*index*/)
      {
        QSettings s;
        QList<QVariant> list;
        for (int w : m_inboxSplitter->sizes())
        {
          list.append(w);
        }
        s.setValue(QStringLiteral("ui/inbox_splitter_sizes"), list);
      });

  // Style the splitter handle
  m_inboxSplitter->setHandleWidth(6);
  m_inboxSplitter->setStyleSheet(R"(
        QSplitter::handle {
            background-color: #38444d;
        }
        QSplitter::handle:hover {
            background-color: #3b82f6;
        }
    )");

  // Add splitter to the layout
  ui->inboxViewLayout->addWidget(m_inboxSplitter);

#ifdef Q_OS_MACOS
  setMacSplitterHandlesArrow(m_inboxSplitter);
#endif
}

void MainWindow::setupEmailReaderSplitter()
{
  QVBoxLayout* outer = ui->emailContentLayout;
  outer->removeWidget(ui->EmailHeader);
  outer->removeWidget(ui->AttachmentsContainer);
  outer->removeWidget(ui->EmailBodyText);

  auto* topPane = new QWidget(ui->EmailContent);
  auto* topLay = new QVBoxLayout(topPane);
  topLay->setContentsMargins(0, 0, 0, 0);
  topLay->setSpacing(0);
  topLay->addWidget(ui->EmailHeader);
  topLay->addWidget(ui->AttachmentsContainer);

  m_emailReaderSplitter = new QSplitter(Qt::Vertical, ui->EmailContent);
  m_emailReaderSplitter->addWidget(topPane);
  m_emailReaderSplitter->addWidget(ui->EmailBodyText);
  m_emailReaderSplitter->setStretchFactor(0, 0);
  m_emailReaderSplitter->setStretchFactor(1, 1);
  m_emailReaderSplitter->setChildrenCollapsible(false);
  m_emailReaderSplitter->setHandleWidth(6);
  m_emailReaderSplitter->setStyleSheet(R"(
        QSplitter::handle {
            background-color: #38444d;
        }
        QSplitter::handle:hover {
            background-color: #3b82f6;
        }
    )");

  ui->EmailBodyText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  {
    QFont f = ui->EmailBodyText->font();
    f.setStyleStrategy(QFont::StyleStrategy(int(QFont::PreferDefault) | int(QFont::NoFontMerging)));
    ui->EmailBodyText->setFont(f);
  }
  topPane->setMinimumHeight(80);

  QSettings settings;
  if (settings.contains(QStringLiteral("ui/email_reader_splitter_sizes")))
  {
    const QList<QVariant> sizesVar = settings.value(QStringLiteral("ui/email_reader_splitter_sizes")).toList();
    QList<int> sz;
    for (const QVariant& v : sizesVar)
    {
      sz.append(v.toInt());
    }
    if (sz.size() >= 2)
    {
      m_emailReaderSplitter->setSizes(sz);
    }
    else
    {
      m_emailReaderSplitter->setSizes({ 200, 480 });
    }
  }
  else
  {
    m_emailReaderSplitter->setSizes({ 200, 480 });
  }

  connect(
      m_emailReaderSplitter,
      &QSplitter::splitterMoved,
      this,
      [this](int, int)
      {
        QSettings s;
        QList<QVariant> list;
        for (int h : m_emailReaderSplitter->sizes())
        {
          list.append(h);
        }
        s.setValue(QStringLiteral("ui/email_reader_splitter_sizes"), list);
      });

  outer->addWidget(m_emailReaderSplitter);

#ifdef Q_OS_MACOS
  setMacSplitterHandlesArrow(m_emailReaderSplitter);
#endif
}

void MainWindow::onLogInButtonClicked()
{
  startOAuthFlow();
}

void MainWindow::startOAuthFlow()
{
  // Validate OAuth credentials are configured
  if (!OAuthCredentials::isConfigured())
  {
    showError("OAuth Not Configured", OAuthCredentials::getMissingCredentialsMessage());
    return;
  }

  // Set credentials from config
  m_oauthManager->setCredentials(OAuthCredentials::clientId(), OAuthCredentials::clientSecret());

  ui->StatusLabel->setText(tr("Opening browser for authentication..."));

  // Start the automated OAuth flow
  m_oauthManager->startAuthFlow();
}

void MainWindow::onOAuthAuthenticated()
{
  // Get the user email from OAuth manager
  m_currentUser = m_oauthManager->getUserEmail();
  qDebug() << "Authenticated as:" << m_currentUser;

  // Now authenticate with mail servers using the OAuth token
  setUIEnabled(false);
  ui->StatusLabel->setText(tr("Connecting to mail servers..."));

  m_oauthManager->ensureValidAccessToken(
      [this](std::optional<std::string> tokenOpt)
      {
        if (!tokenOpt)
        {
          setUIEnabled(true);
          ui->StatusLabel->setText(tr("No OAuth token available"));
          qWarning() << "No OAuth token available";
          return;
        }
        authenticateWithOAuthAsync(*tokenOpt);
      });
}

void MainWindow::onOAuthFailed(const QString& error)
{
  setUIEnabled(true);
  ui->StatusLabel->setText(tr("Authentication failed"));
  showError(tr("Authentication Failed"), error);
}

void MainWindow::completeMailAuthenticationUi(bool success)
{
  setUIEnabled(true);
  if (success)
  {
    ui->StatusLabel->clear();
    ui->PasswordLine->clear();  // Don't leave the password in the form widget.
    // Persist the just-authenticated account so the user can switch back
    // to it next session and so it appears in the account-switcher menu.
    if (m_session)
    {
      m_session->registerActiveAccount(m_currentAuthMethod);
    }
    ui->MainPagesStack->setCurrentIndex(static_cast<int>(EMainPagesIndex::MainPage));
    initializeMainPage();
  }
  else
  {
    // Generic error message — never leak whether the username was valid.
    ui->StatusLabel->setText(QStringLiteral("Sign-in failed. Check your credentials and try again."));
  }
}

void MainWindow::authenticateWithOAuthAsync(const std::string& accessToken)
{
  auto imapClient = m_imapClient.lock();
  auto smtpClient = m_smtpClient.lock();

  if (!imapClient || !smtpClient || (m_ioContext == nullptr) || !m_mailSession)
  {
    qWarning() << "Clients, io_context, or mail session not available";
    completeMailAuthenticationUi(false);
    return;
  }

  std::string username = m_currentUser.toStdString();
  std::string imapServer = m_imapServer.toStdString();
  uint16_t imapPort = static_cast<uint16_t>(m_imapPort);
  std::string smtpServer = m_smtpServer.toStdString();
  uint16_t smtpPort = static_cast<uint16_t>(m_smtpPort);

  std::shared_ptr<MailSessionService::Strand> strand = m_mailSession->imapStrand();

  boost::asio::co_spawn(
      *m_ioContext,
      [this, strand, imapClient, smtpClient, username, imapServer, imapPort, smtpServer, smtpPort, accessToken]() mutable
          -> boost::asio::awaitable<void>
      {
        auto finishUi = [this](bool ok)
        { QMetaObject::invokeMethod(this, [this, ok]() { completeMailAuthenticationUi(ok); }, Qt::QueuedConnection); };

        auto publishState = [this](ImapSessionLinkState s)
        { QMetaObject::invokeMethod(this, [this, s]() { publishImapLinkState(s); }, Qt::QueuedConnection); };

        try
        {
          publishState(ImapSessionLinkState::Connecting);

          // Best-effort cleanup before reconnecting; ignore any teardown error.
          (void)co_await smtpClient->closeConnection();

          qDebug() << "Connecting to IMAP server (on IMAP strand)...";
          const bool imapOk = co_await boost::asio::co_spawn(
              *strand,
              [imapClient, imapServer, imapPort, username, accessToken]() -> boost::asio::awaitable<bool>
              {
                (void)co_await imapClient->closeConnection();
                imapClient->reset();

                auto imapConnectResult = co_await imapClient->asyncConnect(imapServer, imapPort);
                if (!imapConnectResult.has_value())
                {
                  qWarning() << "IMAP connect failed:" << QString::fromStdString(imapConnectResult.error().toString());
                  co_return false;
                }
                qDebug() << "IMAP connected successfully";

                aurora::mail::common::TagGenerator tagGen;
                aurora::mail::imap::command::AuthXOAuth2 imapAuth{ tagGen.next(), username, accessToken };

                qDebug() << "Authenticating IMAP with XOAUTH2...";
                auto imapAuthResult = co_await imapClient->asyncAuthenticate(imapAuth);
                if (!imapAuthResult.has_value())
                {
                  qWarning() << "IMAP auth failed:" << QString::fromStdString(imapAuthResult.error().toString());
                  co_return false;
                }
                qDebug() << "IMAP authenticated successfully";
                co_return true;
              },
              boost::asio::use_awaitable);

          if (!imapOk)
          {
            publishState(ImapSessionLinkState::Disconnected);
            finishUi(false);
            co_return;
          }

          qDebug() << "Connecting to SMTP server...";
          auto smtpConnectResult = co_await smtpClient->asyncConnect(smtpServer, smtpPort);
          if (!smtpConnectResult.has_value())
          {
            qWarning() << "SMTP connect failed:" << QString::fromStdString(smtpConnectResult.error().toString());
            publishState(ImapSessionLinkState::Disconnected);
            finishUi(false);
            co_return;
          }
          qDebug() << "SMTP connected successfully";

          aurora::mail::smtp::command::AuthXOAuth2 smtpAuth{ username, accessToken };

          qDebug() << "Authenticating SMTP with XOAUTH2...";
          auto smtpAuthResult = co_await smtpClient->asyncAuthenticate(smtpAuth);
          if (!smtpAuthResult.has_value())
          {
            qWarning() << "SMTP auth failed:" << QString::fromStdString(smtpAuthResult.error().toString());
            publishState(ImapSessionLinkState::Disconnected);
            finishUi(false);
            co_return;
          }
          qDebug() << "SMTP authenticated successfully";

          publishState(ImapSessionLinkState::Authenticated);
          QMetaObject::invokeMethod(this, [this]() { m_currentAuthMethod = EAuthMethod::OAuth; }, Qt::QueuedConnection);
          finishUi(true);
        }
        catch (const std::exception& e)
        {
          qWarning() << "Authentication error:" << e.what();
          publishState(ImapSessionLinkState::Disconnected);
          finishUi(false);
        }
        catch (...)
        {
          // See loadFolders() for the rationale: a detached coroutine that
          // lets a non-std exception escape calls std::terminate, killing
          // the GUI app without any diagnostic.
          qWarning() << "OAuth authentication: non-std exception";
          publishState(ImapSessionLinkState::Disconnected);
          finishUi(false);
        }
        co_return;
      },
      boost::asio::detached);
}

void MainWindow::publishImapLinkState(ImapSessionLinkState state)
{
  m_imapLinkState = state;
  if (m_mailSessionSignals)
  {
    m_mailSessionSignals->publishLinkState(state);
  }
}

namespace
{

  /**
   * @brief Parses "host[:port]" into a (host, port) pair.
   *
   * Strict allow-list validation per the codebase's input-validation guidelines:
   *  - Host must be non-empty and contain only [A-Za-z0-9.-].
   *  - Port (when present) must be a 16-bit non-zero integer.
   * Returns std::nullopt on any violation; never throws.
   */
  std::optional<std::pair<QString, quint16>> parseHostPort(const QString& raw, quint16 defaultPort)
  {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
    {
      return std::nullopt;
    }
    QString host = trimmed;
    quint16 port = defaultPort;
    const qsizetype colon = trimmed.indexOf(QChar(':'));
    if (colon >= 0)
    {
      host = trimmed.left(colon);
      const QString portStr = trimmed.mid(colon + 1);
      bool ok = false;
      const uint parsed = portStr.toUInt(&ok);
      if (!ok || parsed == 0 || parsed > 65535)
      {
        return std::nullopt;
      }
      port = static_cast<quint16>(parsed);
    }
    if (host.isEmpty())
    {
      return std::nullopt;
    }
    static const QRegularExpression hostRe(QStringLiteral("^[A-Za-z0-9.-]+$"));
    if (!hostRe.match(host).hasMatch())
    {
      return std::nullopt;
    }
    return std::make_pair(host, port);
  }

}  // namespace

void MainWindow::onUsePasswordLoginClicked()
{
  showLoginMethodPage(ELoginMethodPage::PasswordPage);
}

void MainWindow::onBackToOAuthClicked()
{
  showLoginMethodPage(ELoginMethodPage::ChooserPage);
}

void MainWindow::onPasswordSignInClicked()
{
  PasswordCredentialsStorage::Credentials creds;
  creds.email = ui->LoginLine->text().trimmed();
  creds.password = ui->PasswordLine->text();

  // Validate inputs *before* touching the network or storage.
  if (creds.email.isEmpty() || !isValidEmail(creds.email))
  {
    showError(QStringLiteral("Invalid email"), QStringLiteral("Please enter a valid email address."));
    ui->LoginLine->setFocus();
    return;
  }
  if (creds.password.isEmpty())
  {
    showError(QStringLiteral("Password required"), QStringLiteral("Please enter your password or app password."));
    ui->PasswordLine->setFocus();
    return;
  }

  auto imapParsed = parseHostPort(ui->ImapServerLine->text(), 993);
  if (!imapParsed)
  {
    showError(
        QStringLiteral("Invalid IMAP server"),
        QStringLiteral("Use the format host or host:port (e.g. imap.gmail.com:993)."));
    ui->ImapServerLine->setFocus();
    return;
  }
  auto smtpParsed = parseHostPort(ui->SmtpServerLine->text(), 587);
  if (!smtpParsed)
  {
    showError(
        QStringLiteral("Invalid SMTP server"),
        QStringLiteral("Use the format host or host:port (e.g. smtp.gmail.com:587)."));
    ui->SmtpServerLine->setFocus();
    return;
  }
  creds.imapServer = imapParsed->first;
  creds.imapPort = imapParsed->second;
  creds.smtpServer = smtpParsed->first;
  creds.smtpPort = smtpParsed->second;

  // Mirror parsed values into MainWindow state used by the rest of the UI.
  m_imapServer = creds.imapServer;
  m_imapPort = creds.imapPort;
  m_smtpServer = creds.smtpServer;
  m_smtpPort = creds.smtpPort;
  m_currentUser = creds.email;

  setUIEnabled(false);
  ui->StatusLabel->setText(QStringLiteral("Connecting to mail servers..."));

  // Hand off to the io_context. The credentials struct (including the
  // password) is moved into the coroutine and will be cleared at the end
  // of authenticateWithPasswordAsync().
  authenticateWithPasswordAsync(std::move(creds));
}

void MainWindow::authenticateWithPasswordAsync(PasswordCredentialsStorage::Credentials creds)
{
  auto imapClient = m_imapClient.lock();
  auto smtpClient = m_smtpClient.lock();
  if (!imapClient || !smtpClient || (m_ioContext == nullptr) || !m_mailSession)
  {
    qWarning() << "Clients, io_context, or mail session not available";
    completeMailAuthenticationUi(false);
    return;
  }

  const std::string username = creds.email.toStdString();
  const std::string password = creds.password.toStdString();
  const std::string imapServer = creds.imapServer.toStdString();
  const std::string smtpServer = creds.smtpServer.toStdString();
  const uint16_t imapPort = creds.imapPort;
  const uint16_t smtpPort = creds.smtpPort;

  // Capture creds by value so we can persist (or wipe) it after auth completes.
  auto credsToPersist = std::make_shared<PasswordCredentialsStorage::Credentials>(std::move(creds));

  std::shared_ptr<MailSessionService::Strand> strand = m_mailSession->imapStrand();

  boost::asio::co_spawn(
      *m_ioContext,
      [this,
       strand,
       imapClient,
       smtpClient,
       username,
       password,
       imapServer,
       imapPort,
       smtpServer,
       smtpPort,
       credsToPersist]() mutable -> boost::asio::awaitable<void>
      {
        auto finishUi = [this, credsToPersist](bool ok)
        {
          // Marshal completion onto the Qt thread; persist on success,
          // wipe in-memory password either way.
          QMetaObject::invokeMethod(
              this,
              [this, ok, credsToPersist]()
              {
                if (ok)
                {
                  const bool persisted = m_passwordCredentials.save(*credsToPersist);
                  if (!persisted)
                  {
                    qInfo() << "Password not persisted "
                               "(secure storage unavailable).";
                  }
                  m_currentAuthMethod = EAuthMethod::Password;
                }
                // Best-effort wipe; QString is COW so this only clears
                // the local copy, but we don't keep the original.
                credsToPersist->password.fill(QChar(0));
                credsToPersist->password.clear();
                completeMailAuthenticationUi(ok);
              },
              Qt::QueuedConnection);
        };

        auto publishState = [this](ImapSessionLinkState s)
        { QMetaObject::invokeMethod(this, [this, s]() { publishImapLinkState(s); }, Qt::QueuedConnection); };

        try
        {
          publishState(ImapSessionLinkState::Connecting);

          // Best-effort cleanup before reconnecting; ignore any teardown error.
          (void)co_await smtpClient->closeConnection();

          qDebug() << "Connecting to IMAP server (password auth, on IMAP strand)...";
          const bool imapOk = co_await boost::asio::co_spawn(
              *strand,
              [imapClient, imapServer, imapPort, username, password]() -> boost::asio::awaitable<bool>
              {
                (void)co_await imapClient->closeConnection();
                imapClient->reset();

                auto connectResult = co_await imapClient->asyncConnect(imapServer, imapPort);
                if (!connectResult.has_value())
                {
                  qWarning() << "IMAP connect failed:" << QString::fromStdString(connectResult.error().toString());
                  co_return false;
                }

                aurora::mail::common::TagGenerator tagGen;
                // Prefer SASL PLAIN over IMAP LOGIN: same credentials, but the
                // payload is base64-encoded on a single line (still secret —
                // logging is redacted in BaseProtocolClient).
                aurora::mail::imap::command::AuthPlain auth{ tagGen.next(), username, password };
                auto authResult = co_await imapClient->asyncAuthenticate(auth);
                if (!authResult.has_value())
                {
                  qWarning() << "IMAP password auth failed:" << QString::fromStdString(authResult.error().toString());
                  co_return false;
                }
                qDebug() << "IMAP authenticated (password) successfully";
                co_return true;
              },
              boost::asio::use_awaitable);

          if (!imapOk)
          {
            publishState(ImapSessionLinkState::Disconnected);
            finishUi(false);
            co_return;
          }

          qDebug() << "Connecting to SMTP server (password auth)...";
          auto smtpConnectResult = co_await smtpClient->asyncConnect(smtpServer, smtpPort);
          if (!smtpConnectResult.has_value())
          {
            qWarning() << "SMTP connect failed:" << QString::fromStdString(smtpConnectResult.error().toString());
            publishState(ImapSessionLinkState::Disconnected);
            finishUi(false);
            co_return;
          }

          aurora::mail::smtp::command::AuthPlain smtpAuth{ username, password };
          auto smtpAuthResult = co_await smtpClient->asyncAuthenticate(smtpAuth);
          if (!smtpAuthResult.has_value())
          {
            qWarning() << "SMTP password auth failed:" << QString::fromStdString(smtpAuthResult.error().toString());
            publishState(ImapSessionLinkState::Disconnected);
            finishUi(false);
            co_return;
          }
          qDebug() << "SMTP authenticated (password) successfully";

          publishState(ImapSessionLinkState::Authenticated);
          finishUi(true);
        }
        catch (const std::exception& e)
        {
          qWarning() << "Password authentication error:" << e.what();
          publishState(ImapSessionLinkState::Disconnected);
          finishUi(false);
        }
        catch (...)
        {
          // Catch-all: non-std exceptions in a detached coroutine reach the
          // resume frame and end up calling std::terminate, which manifests
          // as a silent process exit for a WIN32_EXECUTABLE GUI app. Log
          // and notify the UI instead.
          qWarning() << "Password authentication: non-std exception";
          publishState(ImapSessionLinkState::Disconnected);
          finishUi(false);
        }
        co_return;
      },
      boost::asio::detached);
}

void MainWindow::onSignOutButtonClicked()
{
  if (!m_session)
  {
    return;
  }
  if (m_session->signOutCurrentAccount() == SessionCoordinator::SignOutOutcome::ShowLoginPage)
  {
    // No remaining account or reconnect failed: return to the login page.
    ui->MainPagesStack->setCurrentIndex(static_cast<int>(EMainPagesIndex::LoginPage));
    showLoginMethodPage(ELoginMethodPage::ChooserPage);
  }
  // Otherwise the coordinator already kicked off a switch to a remaining
  // account; the resulting OAuth/password completion path will land in
  // completeMailAuthenticationUi().
}

void MainWindow::teardownActiveSession(bool destroyPersistentCache)
{
  // Stop IDLE / pump first so no in-flight FETCH races the disconnect.
  stopPolling();

  publishImapLinkState(ImapSessionLinkState::Disconnected);

  try
  {
    if (auto imapClient = m_imapClient.lock())
    {
      imapClient->reset();
    }
    // SMTP cleanup is handled by the next asyncConnect on reconnect; the
    // shared client owns its own teardown semantics.
  }
  catch (const std::exception& e)
  {
    qWarning() << "Error during session teardown:" << e.what();
  }

  // Reset per-account UI state so the next account never sees stale data.
  // Two distinct dispositions for the cache (see teardownActiveSession docs):
  //   * destroyPersistentCache == true  → sign-out: shred on-disk DB and the
  //     per-account AES master key in the keychain, so no future process can
  //     recover the bodies.
  //   * destroyPersistentCache == false → account-switch / add-account: drop
  //     only the in-memory tier and close the SQLite handle. The on-disk file
  //     and the keychain key are preserved, so when the user comes back to
  //     this account the next ensureMessageCacheForCurrentAccount() will
  //     simply re-attach and the user pays no fresh-sync cost.
  const QString outgoingAccount = m_currentUser;
  m_currentMailbox = QStringLiteral("Inbox");
  m_currentMailboxUidValidity = 0;
  m_displayedMessageKey = aurora::mail::app::cache::MessageKey{};
  m_pendingDisplayKey = aurora::mail::app::cache::MessageKey{};
  if (!outgoingAccount.isEmpty())
  {
    if (destroyPersistentCache)
    {
      m_messageCache.invalidateAccount(outgoingAccount);
    }
    else
    {
      m_messageCache.unloadAccount(outgoingAccount);
    }
  }
  else if (destroyPersistentCache)
  {
    // No identifiable account on a sign-out (early failure path): wipe
    // everything to be on the safe side.
    m_messageCache.clear();
  }
  if (m_emailListManager != nullptr)
  {
    m_emailListManager->clearEmailList();
    m_emailListManager->invalidateAllCaches();
  }
  resetEmailReader(QStringLiteral("Select an email to view"));

  // Identity (current user, auth method, in-memory OAuth tokens) is owned
  // by the SessionCoordinator scenario façade.
  if (m_session)
  {
    m_session->resetIdentity();
  }
}

void MainWindow::ensureMessageCacheForCurrentAccount()
{
  if (m_currentUser.isEmpty())
  {
    return;
  }
  if (m_messageCache.hasPersistentTier(m_currentUser))
  {
    return;
  }

  // Provision (or load) the per-account 256-bit AES-GCM master key from the
  // OS keychain. If the keychain is unavailable on this platform/build, the
  // call returns nullopt and we stay memory-only — we never write user mail
  // bodies to disk in plaintext.
  auto key = aurora::mail::app::cache::CacheKeyMaterial::loadOrCreate(m_currentUser);
  if (!key.has_value())
  {
    qInfo() << "Message cache: persistent tier disabled for" << m_currentUser << "(no secure key storage available)";
    return;
  }
  auto persistent = std::make_unique<aurora::mail::app::cache::PersistentMessageCache>(m_currentUser, *key);
  aurora::mail::app::cache::AesGcmCipher::secureWipe(*key);

  if (!persistent->isEnabled())
  {
    qWarning() << "Message cache: failed to open persistent tier for" << m_currentUser << "; staying memory-only";
    return;
  }
  m_messageCache.attachAccount(m_currentUser, std::move(persistent));
  qInfo() << "Message cache: persistent encrypted tier attached for" << m_currentUser;
}

void MainWindow::rebuildAccountMenu()
{
  if (m_session)
  {
    m_session->populateAccountMenu(m_accountMenu);
  }
}

void MainWindow::onAddAccountSelected()
{
  // Tear down the current session but keep the registry intact so the
  // existing accounts remain available after the new sign-in completes.
  teardownActiveSession();

  // We intentionally DO NOT clear the active-email pointer here; if the
  // user cancels the new sign-in (closes the window, etc.) we'd want
  // initializeLoginPage() on the next launch to still resume the prior
  // active account.

  ui->MainPagesStack->setCurrentIndex(static_cast<int>(EMainPagesIndex::LoginPage));
  showLoginMethodPage(ELoginMethodPage::ChooserPage);
}

void MainWindow::onSwitchAccountSelected(const QString& email)
{
  if (!m_session || email.isEmpty() || email == m_currentUser)
  {
    return;
  }
  auto target = m_accountRegistry.findByEmail(email);
  if (!target)
  {
    qWarning() << "Switch requested for unregistered account; ignoring.";
    return;
  }

  teardownActiveSession();
  m_accountRegistry.setActiveEmail(email);
  if (!m_session->reconnectStoredAccount(*target))
  {
    // Could not silently resume — most likely the OAuth refresh token was
    // revoked or the keychain entry vanished. Send the user to the login
    // page so they can re-auth.
    m_accountRegistry.setActiveEmail(QString());
    ui->MainPagesStack->setCurrentIndex(static_cast<int>(EMainPagesIndex::LoginPage));
    showLoginMethodPage(ELoginMethodPage::ChooserPage);
    ui->StatusLabel->setText(tr("Could not resume %1 — please sign in again.").arg(email));
  }
}

void MainWindow::onComposeButtonClicked()
{
  clearComposeFields();
  ui->ContentStack->setCurrentIndex(static_cast<int>(EContentViewIndex::ComposeView));
}

void MainWindow::onInboxButtonClicked()
{
  selectMailbox("INBOX");
  selectNavButton(ui->InboxButton);
}

QString MainWindow::gmailSentMailboxPath()
{
  return QStringLiteral("[Gmail]/Sent Mail");
}

void MainWindow::onSentButtonClicked()
{
  selectMailbox(gmailSentMailboxPath());
  selectNavButton(ui->SentButton);
}

void MainWindow::onDraftsButtonClicked()
{
  selectMailbox("[Gmail]/Drafts");
  selectNavButton(ui->DraftsButton);
}

void MainWindow::onTrashButtonClicked()
{
  selectMailbox("[Gmail]/Trash");
  selectNavButton(ui->TrashButton);
}

void MainWindow::onSidebarToggleClicked()
{
  bool willBeVisible = !ui->Sidebar->isVisible();
  ui->Sidebar->setVisible(willBeVisible);

  // Update button to show arrow direction
  ui->SidebarToggleButton->setText(willBeVisible ? "◀" : "▶");
}

void MainWindow::onFolderButtonClicked(const QString& folderName)
{
  selectMailbox(folderName);
  for (QPushButton* btn : m_folderButtons)
  {
    if (btn->property("folderName").toString() == folderName)
    {
      selectNavButton(btn);
      return;
    }
  }
}

void MainWindow::selectNavButton(QPushButton* button)
{
  for (auto* btn : m_navButtonGroup->buttons())
  {
    btn->setChecked(btn == button);
  }
}

void MainWindow::loadFolders(std::function<void()> afterAppliedOnQtThread)
{
  auto imapClient = m_imapClient.lock();
  if (!imapClient || (m_ioContext == nullptr) || !m_mailSession)
  {
    if (afterAppliedOnQtThread)
    {
      QMetaObject::invokeMethod(
          this,
          [cb = std::move(afterAppliedOnQtThread)]() mutable
          {
            if (cb)
            {
              cb();
            }
          },
          Qt::QueuedConnection);
    }
    return;
  }

  std::shared_ptr<MailSessionService::Strand> strand = m_mailSession->imapStrand();

  boost::asio::co_spawn(
      *m_ioContext,
      [this, strand, imapClient, after = std::move(afterAppliedOnQtThread)]() mutable -> boost::asio::awaitable<void>
      {
        // Detached coroutines that escape an exception terminate the whole
        // process via std::terminate. For a WIN32_EXECUTABLE GUI app that
        // surfaces as a "silent crash" with no message box and no console
        // output. Wrap the body in a catch-all and route the failure back
        // to the Qt thread instead, so the user at least sees a status
        // message and the rest of the app keeps running.
        try
        {
          std::vector<aurora::mail::imap::MailboxInfo> folders = co_await boost::asio::co_spawn(
              *strand,
              [imapClient]() -> boost::asio::awaitable<std::vector<aurora::mail::imap::MailboxInfo>>
              {
                auto result = co_await imapClient->asyncListMailboxes("", "*");
                if (result.has_value())
                {
                  co_return std::move(result.value());
                }
                qWarning() << "Failed to list folders:" << QString::fromStdString(result.error().toString());
                co_return std::vector<aurora::mail::imap::MailboxInfo>{};
              },
              boost::asio::use_awaitable);

          QMetaObject::invokeMethod(
              this,
              [this, folders = std::move(folders), after = std::move(after)]() mutable
              {
                applyFolderList(folders);
                if (after)
                {
                  after();
                }
              },
              Qt::QueuedConnection);
        }
        catch (const std::exception& e)
        {
          const QString reason = QString::fromUtf8(e.what());
          qWarning() << "loadFolders coroutine threw:" << reason;
          QMetaObject::invokeMethod(
              this,
              [this, reason, after = std::move(after)]() mutable
              {
                showStatus(tr("Failed to load folders: %1").arg(reason));
                if (after)
                {
                  after();
                }
              },
              Qt::QueuedConnection);
        }
        catch (...)
        {
          qWarning() << "loadFolders coroutine threw a non-std exception";
          QMetaObject::invokeMethod(
              this,
              [this, after = std::move(after)]() mutable
              {
                showStatus(tr("Failed to load folders (unknown error)"));
                if (after)
                {
                  after();
                }
              },
              Qt::QueuedConnection);
        }
        co_return;
      },
      boost::asio::detached);
}

void MainWindow::applyFolderList(const std::vector<aurora::mail::imap::MailboxInfo>& folders)
{
  qDebug() << "Loaded" << folders.size() << "folders";

  foreach (QPushButton* btn, m_folderButtons)
  {
    ui->foldersLayout->removeWidget(btn);
    btn->deleteLater();
  }

  m_folderButtons.clear();

  for (const auto& folder : folders)
  {
    QString folderName = QString::fromStdString(folder.name);
    QString decodedName = QString::fromStdString(folder.getDecodedName());

    if (!folder.isSelectable())
    {
      continue;
    }

    if (folderName.toUpper() == "INBOX")
    {
      continue;
    }

    if (folder.hasAttribute("\\Sent") || folder.hasAttribute("\\Trash") || folder.hasAttribute("\\Drafts"))
    {
      continue;
    }

    QString displayName = decodedName;
    if (displayName.startsWith("[Gmail]/"))
    {
      displayName = displayName.mid(8);
    }

    QPushButton* btn = createFolderButton(folderName, displayName);

    int insertIndex = ui->foldersLayout->count() - 1;
    if (insertIndex < 0)
      insertIndex = 0;
    ui->foldersLayout->insertWidget(insertIndex, btn);
  }
}

void MainWindow::selectMailbox(const QString& mailbox)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  // Update UI immediately for responsiveness.
  // NOTE: switching folders is NOT a cache-invalidation event. Bodies for the
  // previous folder remain valid and worth keeping for backward navigation.
  // The body cache is only invalidated on EXPUNGE, MOVE-source, UIDVALIDITY
  // change, or sign-out — see invalidate*() call sites below.
  if (mailbox != m_currentMailbox)
  {
    // Reset the captured UIDVALIDITY; the upcoming SELECT will refresh it.
    // Until then we deliberately serve no body cache entries for this folder.
    m_currentMailboxUidValidity = 0;
  }
  m_currentMailbox = mailbox;
  if (m_emailListManager != nullptr)
  {
    m_emailListManager->setCurrentMailbox(mailbox);
  }
  ui->ContentStack->setCurrentIndex(static_cast<int>(EContentViewIndex::InboxView));

  // Check if we have cached data for this mailbox
  if ((m_emailListManager != nullptr) && m_emailListManager->hasValidCache(mailbox))
  {
    // Display cached data immediately (instant switch!) — also clear the reader so
    // attachments/body from a previously opened message in the prior mailbox don't
    // linger while the user picks a new email here.
    resetEmailReader(QStringLiteral("Select an email to view"));
    m_emailListManager->displayCachedEmails(mailbox);
    showStatus("Syncing...");
  }
  else
  {
    resetEmailReader(QStringLiteral("Loading %1...").arg(mailbox));
    if (m_emailListManager != nullptr)
    {
      m_emailListManager->clearEmailList();
    }
  }

  // Enqueue sync operation (will fetch/update from server)
  enqueueImapOperation({ ImapOpType::SelectMailbox, mailbox, QString(), true });
}

void MainWindow::loadEmails(bool append)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  if (!append)
  {
    m_mailListScrollReadyForMore = true;
  }

  enqueueImapOperation({ ImapOpType::FetchMailboxPage, QString(), QString(), append });
}

void MainWindow::loadMoreEmails()
{
  loadEmails(true);  // append = true
}

void MainWindow::onLoadMoreClicked()
{
  // loadMoreEmails will handle busy state via the queue
  loadMoreEmails();
}

void MainWindow::onMailListScrollValueChanged(int value)
{
  Q_UNUSED(value);
  if ((m_emailListManager == nullptr) || !m_emailListManager->canLoadMore())
  {
    return;
  }
  if (m_mailSession && m_mailSession->imapBusy())
  {
    return;
  }

  QScrollBar* sb = ui->MailListArea->verticalScrollBar();
  const int vmax = sb->maximum();
  if (vmax <= 0)
  {
    return;
  }

  const int margin = qMin(150, qMax(48, vmax / 6));
  if (sb->value() < vmax - margin)
  {
    m_mailListScrollReadyForMore = true;
    return;
  }
  if (!m_mailListScrollReadyForMore)
  {
    return;
  }
  m_mailListScrollReadyForMore = false;
  loadMoreEmails();
}

void MainWindow::onEmailItemClicked(const QString& uid)
{
  loadEmailContent(uid);
}

void MainWindow::loadEmailContent(const QString& uid)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  const auto key = emailMessageKey(uid);

  // Bail if we're already showing this exact (account, mailbox, uidValidity, uid).
  if (m_displayedMessageKey.isValid() && key == m_displayedMessageKey)
  {
    return;
  }

  // Record the user's latest intent before any await point. applyParsedEmailBodyOnQt
  // uses this to discard UI updates from older, now-stale FETCH responses when
  // the user clicks through several messages faster than the network can answer.
  m_pendingDisplayKey = key;

  // Tier 1/Tier 2 cache lookup. Only attempt if UIDVALIDITY is known (key.isValid):
  // before SELECT completes we cannot prove the cached entry refers to the same UID.
  if (key.isValid())
  {
    if (auto cached = m_messageCache.tryGet(key))
    {
      applyParsedEmailBodyOnQt(cached->content, uid);
      return;
    }
  }

  // Cache the in-flight reservation so concurrent clicks for the same UID coalesce
  // into one IMAP FETCH. Replaces the ad-hoc m_pendingEmailBodyKey field.
  if (key.isValid() && !m_messageCache.reservePending(key))
  {
    // Another call is already fetching this exact key.
    return;
  }

  // Update UI immediately for responsiveness. resetEmailReader() also clears the
  // attachments bar so the previously viewed email's attachments don't stay
  // visible while the new body is being fetched.
  showStatus("Loading email...", 0);
  resetEmailReader(QStringLiteral("Loading..."));

  // Enqueue the operation
  enqueueImapOperation({ ImapOpType::LoadEmail, uid, QString(), true });
}

void MainWindow::onRefreshButtonClicked()
{
  // Refresh = reload current mailbox. Use selectMailbox to go through queue.
  if (!m_currentMailbox.isEmpty())
  {
    selectMailbox(m_currentMailbox);
  }
}

void MainWindow::onSearchTextChanged(const QString& text)
{
  if (m_emailListManager != nullptr)
  {
    m_emailListManager->setSearchFilter(text);
  }
}

void MainWindow::onReplyButtonClicked()
{
  if ((m_emailListManager == nullptr) || m_emailListManager->selectedUid().isEmpty())
    return;

  QString subject = ui->EmailSubjectLabel->text();
  QString from = ui->EmailMetaLabel->text().replace("From: ", "");

  // Get original message date from the email item
  QString dateStr;
  const EmailSummary* summary = m_emailListManager->summaryForUid(m_emailListManager->selectedUid());
  if (summary != nullptr)
  {
    QDateTime date = summary->date;
    dateStr = date.toString("ddd, MMM d, yyyy 'at' h:mm AP");
  }

  clearComposeFields();
  ui->ToLine->setText(from);
  ui->SubjectLine->setText(subject.startsWith("Re: ") ? subject : "Re: " + subject);

  // Format reply body like real email clients:
  // "On [date], [sender] wrote:" followed by quoted original text
  QString originalText = ui->EmailBodyText->toPlainText();
  QStringList lines = originalText.split('\n');
  QString quotedText;
  foreach (const QString& line, lines)
  {
    quotedText += "> " + line + "\n";
  }

  QString replyHeader = QString("On %1, %2 wrote:\n").arg(dateStr, from);
  ui->ComposeBodyText->setPlainText("\n\n" + replyHeader + quotedText);

  // Position cursor at the start for user to type their reply
  QTextCursor cursor = ui->ComposeBodyText->textCursor();
  cursor.movePosition(QTextCursor::Start);
  ui->ComposeBodyText->setTextCursor(cursor);

  // Store the Message-ID for threading
  m_replyToMessageId = m_emailListManager->selectedMessageId();

  ui->ContentStack->setCurrentIndex(static_cast<int>(EContentViewIndex::ComposeView));
}

void MainWindow::onDeleteButtonClicked()
{
  if ((m_emailListManager == nullptr) || m_emailListManager->selectedUid().isEmpty())
    return;

  if (QMessageBox::question(
          this, "Delete Email", "Are you sure you want to delete this email?", QMessageBox::Yes | QMessageBox::No) ==
      QMessageBox::Yes)
  {
    deleteEmail(m_emailListManager->selectedUid());
  }
}

void MainWindow::onMoveButtonClicked()
{
  if ((m_emailListManager == nullptr) || m_emailListManager->selectedUid().isEmpty())
    return;

  // Get list of available folders from the loaded folder buttons
  QStringList folders;

  // Add predefined folders
  folders << "INBOX" << "[Gmail]/Sent Mail" << "[Gmail]/Drafts" << "[Gmail]/Trash" << "[Gmail]/Spam";

  // Add dynamically loaded folders
  foreach (QPushButton* btn, m_folderButtons)
  {
    QString folderName = btn->property("folderName").toString();
    if (!folderName.isEmpty() && folderName != m_currentMailbox && !folders.contains(folderName))
    {
      folders << folderName;
    }
  }

  // Remove current mailbox from the list
  folders.removeAll(m_currentMailbox);

  if (folders.isEmpty())
  {
    showError("Move", "No other folders available.");
    return;
  }

  bool ok;
  QString destination = QInputDialog::getItem(this, "Move to Folder", "Select destination folder:", folders, 0, false, &ok);

  if (ok && !destination.isEmpty())
  {
    moveEmail(m_emailListManager->selectedUid(), destination);
  }
}

void MainWindow::onToggleReadClicked()
{
  if ((m_emailListManager == nullptr) || m_emailListManager->selectedUid().isEmpty())
    return;

  bool isCurrentlyRead = false;
  isCurrentlyRead = !m_emailListManager->isEmailUnread(m_emailListManager->selectedUid());

  // Toggle read status
  markEmailAsRead(m_emailListManager->selectedUid(), !isCurrentlyRead);

  // Update UI
  m_emailListManager->setEmailUnread(m_emailListManager->selectedUid(), isCurrentlyRead);

  showStatus(isCurrentlyRead ? "Marked as unread" : "Marked as read");
}

void MainWindow::onToggleStarClicked()
{
  if ((m_emailListManager == nullptr) || m_emailListManager->selectedUid().isEmpty())
    return;

  toggleEmailFlag(m_emailListManager->selectedUid(), "\\Flagged");
  showStatus("Star toggled");
}

void MainWindow::markEmailAsRead(const QString& uid, bool read)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  enqueueImapOperation({ ImapOpType::MarkRead, uid, QString(), read });
}

void MainWindow::deleteEmail(const QString& uid)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  showStatus("Queuing delete...");
  enqueueImapOperation({ ImapOpType::Delete, uid, QString(), true });
}

void MainWindow::moveEmail(const QString& uid, const QString& destinationMailbox)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  showStatus(QString("Queuing move to %1...").arg(destinationMailbox));
  enqueueImapOperation({ ImapOpType::Move, uid, destinationMailbox, true });
}

void MainWindow::toggleEmailFlag(const QString& uid, const QString& flag)
{
  if (!m_imapClient.lock() || (m_ioContext == nullptr))
    return;

  enqueueImapOperation({ ImapOpType::ToggleFlag, uid, flag, true });
}

void MainWindow::onSendButtonClicked()
{
  QString to = ui->ToLine->text().trimmed();
  QString subject = ui->SubjectLine->text().trimmed();
  QString body = ui->ComposeBodyText->toPlainText();

  if (to.isEmpty())
  {
    showError("Validation Error", "Please enter at least one recipient.");
    return;
  }

  if (!validateEmails(to))
    return;

  auto smtpClient = m_smtpClient.lock();
  if (!smtpClient || (m_ioContext == nullptr))
  {
    showError("Error", "SMTP client not available");
    return;
  }

  setUIEnabled(false);
  showStatus("Sending email...", 0);

  // Build the email message
  using namespace aurora::mail::common::mail;

  MailMessageBuilder builder;
  builder.from(MailAddress{ m_currentUser.toStdString() }).subject(subject.toStdString()).body(body.toStdString());

  QStringList recipients = to.split(s_splitRegex, Qt::SkipEmptyParts);
  std::vector<std::string> recipientList;
  foreach (const QString& recipient, recipients)
  {
    builder.to(MailAddress{ recipient.trimmed().toStdString() });
    recipientList.push_back(recipient.trimmed().toStdString());
  }

  // Add attachments
  foreach (const QString& attachmentPath, m_selectedAttachments)
  {
    builder.addAttachment(MailAttachment{ attachmentPath.toStdString() });
    qDebug() << "Adding attachment:" << attachmentPath;
  }

  // Add threading headers if this is a reply
  if (!m_replyToMessageId.isEmpty())
  {
    builder.replyTo(m_replyToMessageId.toStdString());
    qDebug() << "Setting In-Reply-To:" << m_replyToMessageId;
  }

  auto mailResult = builder.build();
  if (!mailResult)
  {
    setUIEnabled(true);
    showError("Build Failed", QString::fromStdString(mailResult.error()));
    return;
  }

  auto mailMessage = mailResult.value();

  // Gmail and most servers close idle SMTP connections after a short time while
  // IMAP stays up (IDLE). Sending on a stale socket yields read timeouts or
  // broken pipe — always establish a fresh SMTP session before MAIL FROM.
  m_oauthManager->ensureValidAccessToken(
      [this, smtpClient, mailMessage = std::move(mailMessage)](std::optional<std::string> tokenOpt) mutable
      {
        if (!tokenOpt)
        {
          QMetaObject::invokeMethod(
              this,
              [this]()
              {
                setUIEnabled(true);
                showError("Send Failed", "Not signed in or the session expired. Please sign in again.");
              },
              Qt::QueuedConnection);
          return;
        }

        const std::string accessToken = std::move(*tokenOpt);
        const std::string username = m_currentUser.toStdString();
        const std::string smtpServer = m_smtpServer.toStdString();
        const uint16_t smtpPort = static_cast<uint16_t>(m_smtpPort);

        boost::asio::co_spawn(
            *m_ioContext,
            [this, smtpClient, mailMessage = std::move(mailMessage), accessToken, username, smtpServer, smtpPort]() mutable
                -> boost::asio::awaitable<void>
            {
              qDebug() << "Sending email: opening SMTP session (connect + AUTH + send)...";

              try
              {
                // Best-effort cleanup of any prior socket before opening a new
                // session; ignore the teardown result.
                (void)co_await smtpClient->closeConnection();

                auto connectResult = co_await smtpClient->asyncConnect(smtpServer, smtpPort);
                if (!connectResult.has_value())
                {
                  qWarning() << "SMTP connect failed:" << QString::fromStdString(connectResult.error().toString());
                  QMetaObject::invokeMethod(
                      this,
                      [this]()
                      {
                        setUIEnabled(true);
                        showError("Send Failed", "Could not connect to the mail server.");
                      },
                      Qt::QueuedConnection);
                  co_return;
                }

                aurora::mail::smtp::command::AuthXOAuth2 smtpAuth{ username, accessToken };
                auto authResult = co_await smtpClient->asyncAuthenticate(smtpAuth);
                if (!authResult.has_value())
                {
                  qWarning() << "SMTP auth failed:" << QString::fromStdString(authResult.error().toString());
                  QMetaObject::invokeMethod(
                      this,
                      [this]()
                      {
                        setUIEnabled(true);
                        showError("Send Failed", "SMTP authentication failed. Please sign in again.");
                      },
                      Qt::QueuedConnection);
                  co_return;
                }

                auto sendResult = co_await smtpClient->asyncSendMail(mailMessage);
                const bool ok = sendResult.has_value();
                if (!ok)
                {
                  qWarning() << "Failed to send email:" << QString::fromStdString(sendResult.error().toString());
                }
                else
                {
                  qDebug() << "Email sent successfully";
                }

                QMetaObject::invokeMethod(
                    this,
                    [this, ok]()
                    {
                      setUIEnabled(true);
                      if (ok)
                      {
                        showStatus("Email sent successfully!");
                        clearComposeFields();
                        ui->ContentStack->setCurrentIndex(static_cast<int>(EContentViewIndex::InboxView));
                        // Sent copy is on the server in Sent, not in the SELECTed INBOX — IDLE won't
                        // refresh Sent. Invalidate Sent cache and reload if the user was viewing Sent.
                        const QString sentPath = gmailSentMailboxPath();
                        if (m_emailListManager)
                        {
                          m_emailListManager->invalidateMailboxCache(sentPath);
                          if (m_currentMailbox.compare(sentPath, Qt::CaseInsensitive) == 0)
                          {
                            loadEmails(false);
                          }
                        }
                      }
                      else
                      {
                        showError("Send Failed", "Failed to send the email. Please try again.");
                      }
                    },
                    Qt::QueuedConnection);
              }
              catch (const std::exception& e)
              {
                qWarning() << "SMTP send coroutine threw:" << e.what();
                QMetaObject::invokeMethod(
                    this,
                    [this, msg = QString::fromUtf8(e.what())]()
                    {
                      setUIEnabled(true);
                      showError("Send Failed", QString("Unexpected error: %1").arg(msg));
                    },
                    Qt::QueuedConnection);
              }
              catch (...)
              {
                // Catch-all keeps a non-std exception from terminating the
                // GUI app silently. See loadFolders() for context.
                qWarning() << "SMTP send coroutine threw a non-std exception";
                QMetaObject::invokeMethod(
                    this,
                    [this]()
                    {
                      setUIEnabled(true);
                      showError("Send Failed", "Unexpected error while sending.");
                    },
                    Qt::QueuedConnection);
              }
              co_return;
            },
            boost::asio::detached);
      });
}

void MainWindow::onAttachButtonClicked()
{
  selectAttachments();
}

void MainWindow::onCloseComposeButtonClicked()
{
  ui->ContentStack->setCurrentIndex(static_cast<int>(EContentViewIndex::InboxView));
}

void MainWindow::onSettingsButtonClicked()
{
  aurora::mail::ui::AiSettingsDialog dlg(m_aiService.get(), this);
  dlg.exec();
}

void MainWindow::onAIAssistButtonClicked()
{
  QString body = ui->ComposeBodyText->toPlainText().trimmed();

  if (body.isEmpty())
  {
    showError("AI Assist", "Please write some text first.");
    return;
  }

  if (!m_aiService->isConfigured())
  {
    showError("AI Assist", "Add your Google AI Studio API key in Settings (sidebar) to use AI features.");
    onSettingsButtonClicked();
    return;
  }

  // Create and show the AI dialog
  auto* dialog = new aurora::mail::ui::AIAssistantDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setOriginalText(body);

  // Connect AI service signals to dialog
  connect(
      m_aiService.get(),
      &AIService::grammarCheckCompleted,
      dialog,
      [dialog](const AIService::Result& result)
      {
        if (result.success)
        {
          dialog->setImprovedText(result.improvedText);
          dialog->showComparison();
        }
        else
        {
          dialog->showError(result.errorMessage);
        }
      });

  connect(
      m_aiService.get(),
      &AIService::textImprovementCompleted,
      dialog,
      [dialog](const AIService::Result& result)
      {
        if (result.success)
        {
          dialog->setImprovedText(result.improvedText);
          dialog->showComparison();
        }
        else
        {
          dialog->showError(result.errorMessage);
        }
      });

  // Connect mode changes to trigger new requests
  connect(
      dialog,
      &aurora::mail::ui::AIAssistantDialog::modeChanged,
      this,
      [this, body](aurora::mail::ui::AIAssistantDialog::Mode mode)
      {
        switch (mode)
        {
          case aurora::mail::ui::AIAssistantDialog::Mode::GrammarCheck: m_aiService->checkGrammar(body); break;
          case aurora::mail::ui::AIAssistantDialog::Mode::ImproveWriting: m_aiService->improveWriting(body); break;
          case aurora::mail::ui::AIAssistantDialog::Mode::MakeFormal: m_aiService->makeMoreFormal(body); break;
          case aurora::mail::ui::AIAssistantDialog::Mode::MakeConcise: m_aiService->makeConcise(body); break;
        }
      });

  // Handle dialog result
  connect(
      dialog,
      &QDialog::accepted,
      this,
      [this, dialog]()
      {
        if (dialog->changesAccepted())
        {
          ui->ComposeBodyText->setPlainText(dialog->getResultText());
          showStatus("AI suggestions applied");
        }
      });

  dialog->show();

  // Do NOT trigger an AI request on open. The dialog starts in its idle
  // state — the user must pick an action and click Generate to actually
  // call the AI. This makes mode selection (and any retries) a single,
  // explicit, intentional gesture rather than firing a network request
  // every time the combo box changes.
}

void MainWindow::clearComposeFields()
{
  ui->ToLine->clear();
  ui->SubjectLine->clear();
  ui->ComposeBodyText->clear();
  ui->AttachmentLabel->clear();
  m_selectedAttachments.clear();
  m_replyToMessageId.clear();  // Clear reply threading context
}

void MainWindow::selectAttachments()
{
  QStringList files = QFileDialog::getOpenFileNames(this, "Attach Files");
  m_selectedAttachments = files.toVector();

  ui->AttachmentLabel->setText(
      m_selectedAttachments.isEmpty() ? "" : QString("%1 file(s) attached").arg(m_selectedAttachments.size()));
}

void MainWindow::resetEmailReader(const QString& placeholder)
{
  ui->EmailSubjectLabel->setText(placeholder);
  ui->EmailMetaLabel->clear();
  ui->EmailBodyText->clear();

  // Release any in-flight reservation we might have held for the previously
  // displayed message so a future click on the same UID is allowed to retry.
  if (m_displayedMessageKey.isValid())
  {
    m_messageCache.releasePending(m_displayedMessageKey);
  }
  m_displayedMessageKey = aurora::mail::app::cache::MessageKey{};

  m_currentEmailAttachments.clear();
  displayAttachments();
}

void MainWindow::displayAttachments()
{
  // Clear existing attachment buttons
  QLayout* layout = ui->AttachmentsContainer->layout();
  while (layout->count() > 0)
  {
    QLayoutItem* item = layout->takeAt(0);
    if (item->widget() != nullptr)
    {
      item->widget()->deleteLater();
    }
    delete item;
  }

  // Hide container if no attachments
  if (m_currentEmailAttachments.isEmpty())
  {
    ui->AttachmentsContainer->hide();
    return;
  }

  ui->AttachmentsContainer->show();

  // Add attachment icon label
  QLabel* iconLabel = new QLabel(tr("Attachments:"), this);
  layout->addWidget(iconLabel);

  // Create buttons for each attachment
  for (int i = 0; i < m_currentEmailAttachments.size(); ++i)
  {
    const AttachmentInfo& att = m_currentEmailAttachments[i];

    // Format size
    QString sizeStr;
    if (att.size < 1024)
    {
      sizeStr = QString("%1 B").arg(att.size);
    }
    else if (att.size < static_cast<qint64>(1024) * 1024)
    {
      sizeStr = QString("%1 KB").arg(att.size / 1024);
    }
    else
    {
      sizeStr = QString("%1 MB").arg(att.size / (static_cast<qint64>(1024) * 1024));
    }

    QPushButton* btn = new QPushButton(QString("%1 (%2)").arg(att.filename, sizeStr), this);
    btn->setObjectName("AttachmentButton");
    btn->setCursor(kInteractiveCursorShape);
    btn->setStyleSheet(
        "QPushButton { "
        "  background-color: #243447; "
        "  border: 1px solid #38444d; "
        "  border-radius: 4px; "
        "  padding: 6px 12px; "
        "  color: #1d9bf0; "
        "} "
        "QPushButton:hover { "
        "  background-color: #2c3e50; "
        "}");

    connect(btn, &QPushButton::clicked, this, [this, i]() { saveAttachment(i); });

    layout->addWidget(btn);
  }

  // Add spacer to push buttons left
  static_cast<QHBoxLayout*>(layout)->addStretch();
}

void MainWindow::saveAttachment(int index)
{
  if (index < 0 || index >= m_currentEmailAttachments.size())
  {
    return;
  }

  const AttachmentInfo& att = m_currentEmailAttachments[index];

  QString savePath = QFileDialog::getSaveFileName(this, "Save Attachment", att.filename, "All Files (*)");

  if (savePath.isEmpty())
  {
    return;  // User cancelled
  }

  QFile file(savePath);
  if (file.open(QIODevice::WriteOnly))
  {
    file.write(att.data);
    file.close();
    showStatus(QString("Saved: %1").arg(att.filename));
  }
  else
  {
    showError("Save Failed", QString("Could not save file: %1").arg(file.errorString()));
  }
}

bool MainWindow::validateEmails(const QString& emails)
{
  QStringList emailList = emails.split(s_splitRegex, Qt::SkipEmptyParts);

  if (emailList.isEmpty())
  {
    showError("Validation Error", "Please enter at least one recipient.");
    return false;
  }

  QStringList invalidEmails;
  foreach (const QString& email, emailList)
  {
    if (!isValidEmail(email))
    {
      invalidEmails.append(email);
    }
  }

  if (!invalidEmails.isEmpty())
  {
    showError("Invalid Email", "Invalid email addresses: " + invalidEmails.join(", "));
    return false;
  }

  return true;
}

// TODO: remove and use the one from AuroraMailEngine
bool MainWindow::isValidEmail(const QString& email)
{
  return s_emailRegex.match(email).hasMatch();
}

void MainWindow::startPolling()
{
  if (!m_mailSession)
  {
    return;
  }
  m_mailSession->startPolling();
}

void MainWindow::stopPolling()
{
  if (!m_mailSession)
  {
    return;
  }

  qDebug() << "Stopping IMAP IDLE";
  m_mailSession->requestStopIdleLoop();

  if (!m_mailSession->idleLoopRunning())
  {
    return;
  }

  QEventLoop loop;
  QTimer stopTimeout;
  stopTimeout.setSingleShot(true);
  QObject::connect(&stopTimeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  stopTimeout.start(10000);

  QMetaObject::Connection conn = QObject::connect(this, &MainWindow::idleLoopStopped, &loop, &QEventLoop::quit);
  loop.exec();
  QObject::disconnect(conn);

  if (m_mailSession->idleLoopRunning())
  {
    qWarning() << "IDLE: Failed to stop in time";
  }
}

void MainWindow::resumeIdle()
{
  if (!m_mailSession)
  {
    return;
  }
  m_mailSession->resumeIdle();
}

void MainWindow::enqueueImapOperation(ImapOperation op)
{
  if (!m_mailSession)
  {
    return;
  }
  m_mailSession->enqueueOperation(std::move(op));
}

ImapSessionCallbacks MainWindow::buildImapSessionCallbacks()
{
  ImapSessionCallbacks c;
  c.onIdleRawNotification = [this](const QString& notification)
  {
    QMetaObject::invokeMethod(
        this, [this, notification]() { handleIdleServerNotification(notification); }, Qt::QueuedConnection);
  };
  // Call directly from io_context strand — resumeIdle only touches controller atomics (no QObject).
  c.onPumpQueueDrainedResumeIdle = [this]()
  {
    if (m_mailSession)
    {
      m_mailSession->resumeIdle();
    }
  };
  c.onPumpIdleWaitTimeout = [this]()
  {
    QMetaObject::invokeMethod(
        this, [this]() { showStatus("Mail connection busy; try again.", 5000); }, Qt::QueuedConnection);
  };
  c.onIdleLoopFinished = [this]()
  { QMetaObject::invokeMethod(this, [this]() { emit idleLoopStopped(); }, Qt::QueuedConnection); };
  return c;
}

void MainWindow::handleIdleServerNotification(const QString& notification)
{
  // EXISTS / EXPUNGE / FETCH refer to the *currently SELECTed* mailbox only.
  // SMTP send stores the copy in Sent — IDLE on INBOX will usually not emit EXISTS for that.
  if (notification.contains(QStringLiteral("EXISTS"), Qt::CaseInsensitive))
  {
    QRegularExpression existsRegex(R"(\*\s+(\d+)\s+EXISTS)", QRegularExpression::CaseInsensitiveOption);
    auto match = existsRegex.match(notification);
    int newCount = match.hasMatch() ? match.captured(1).toInt() : 0;

    if (m_emailListManager != nullptr)
    {
      int currentCount = m_emailListManager->mailboxMessageCount();
      if (newCount > currentCount)
      {
        qDebug() << "IDLE: Mailbox count updated from" << currentCount << "to" << newCount;
        m_emailListManager->setMailboxMessageCount(newCount);
      }
    }
    showStatus("New email received!", 3000);
    loadEmails();
  }
  else if (notification.contains(QStringLiteral("EXPUNGE"), Qt::CaseInsensitive))
  {
    if (m_emailListManager != nullptr)
    {
      int currentCount = m_emailListManager->mailboxMessageCount();
      if (currentCount > 0)
      {
        m_emailListManager->setMailboxMessageCount(currentCount - 1);
      }
    }
    showStatus("Email deleted on server", 3000);
    loadEmails();
  }
  else if (notification.contains(QStringLiteral("FETCH"), Qt::CaseInsensitive))
  {
    showStatus("Email flags updated", 3000);
    loadEmails();
  }
}

boost::asio::awaitable<void> MainWindow::dispatchImapOperation(const ImapOperation& op)
{
  auto imapClient = m_imapClient.lock();
  if (!imapClient || (m_ioContext == nullptr))
  {
    co_return;
  }

  switch (op.type)
  {
    case ImapOpType::SelectMailbox:
    {
      const QString mailbox = op.param1;
      QMetaObject::invokeMethod(
          this,
          [this]()
          {
            if (m_emailListManager)
            {
              m_emailListManager->setMailboxMessageCount(0);
              m_emailListManager->setOldestLoadedSeqNum(0);
            }
          },
          Qt::BlockingQueuedConnection);

      std::string mailboxStr = mailbox.toStdString();
      auto result = co_await imapClient->asyncSelectCondstore(mailboxStr);
      int messageCount = -1;
      quint32 newUidValidity = 0;
      if (result.has_value())
      {
        qDebug() << "Mailbox selected:" << QString::fromStdString(mailboxStr) << "with" << result.value().exists
                 << "messages";
        messageCount = static_cast<int>(result.value().exists);
        newUidValidity = static_cast<quint32>(result.value().uidvalidity);
      }
      else
      {
        qWarning() << "Failed to select mailbox:" << QString::fromStdString(result.error().toString());
      }

      QMetaObject::invokeMethod(
          this,
          [this, messageCount, newUidValidity, mailbox]()
          {
            if (messageCount < 0)
            {
              resetEmailReader(QStringLiteral("Failed to select mailbox"));
            }
            else
            {
              if (m_emailListManager)
              {
                m_emailListManager->setMailboxMessageCount(messageCount);
              }
              // RFC 3501: a UIDVALIDITY change MUST invalidate cached UIDs.
              // We compare against the previously captured value for THIS
              // (account, mailbox) and drop the entire mailbox if changed.
              if (newUidValidity != 0 && m_currentMailboxUidValidity != 0 && newUidValidity != m_currentMailboxUidValidity)
              {
                qWarning() << "UIDVALIDITY changed for" << mailbox << "from" << m_currentMailboxUidValidity << "to"
                           << newUidValidity << "; invalidating cache";
                m_messageCache.invalidateMailbox(m_currentUser, mailbox);
                if (m_emailListManager)
                {
                  m_emailListManager->invalidateMailboxCache(mailbox);
                }
              }
              m_currentMailboxUidValidity = newUidValidity;
              ensureMessageCacheForCurrentAccount();
              resetEmailReader(QStringLiteral("Select an email to view"));
            }
          },
          Qt::BlockingQueuedConnection);

      if (messageCount >= 0)
      {
        co_await fetchMailboxPageAwaitable(false, messageCount);
      }
      break;
    }
    case ImapOpType::FetchMailboxPage: co_await fetchMailboxPageAwaitable(op.boolParam, -1); break;

    case ImapOpType::LoadEmail:
    {
      const QString uid = op.param1;
      std::string uidStr = uid.toStdString();
      std::string response;
      auto fetchResult = co_await imapClient->asyncUidFetchMail(uidStr, "(ENVELOPE BODY[])");
      if (fetchResult.has_value())
      {
        response = std::move(fetchResult.value());
      }
      else
      {
        qWarning() << "Failed to fetch email content:" << QString::fromStdString(fetchResult.error().toString());
      }

      ParsedEmailContent parsed;
      if (!response.empty())
      {
        parsed = EmailParser::parseFullEmailContent(response);
      }

      QMetaObject::invokeMethod(
          this, [this, parsed, uid]() { applyParsedEmailBodyOnQt(parsed, uid); }, Qt::BlockingQueuedConnection);
      break;
    }
    case ImapOpType::MarkRead:
    {
      std::string uidStr = op.param1.toStdString();
      std::string flagAction = op.boolParam ? "+FLAGS.SILENT (\\Seen)" : "-FLAGS.SILENT (\\Seen)";
      auto result = co_await imapClient->asyncUidStoreMail(uidStr, flagAction);
      if (!result.has_value())
      {
        qWarning() << "Failed to update read status:" << QString::fromStdString(result.error().toString());
      }
      break;
    }
    case ImapOpType::Delete:
    {
      const QString uid = op.param1;
      QMetaObject::invokeMethod(this, [this]() { showStatus("Deleting email...", 0); }, Qt::BlockingQueuedConnection);
      std::string uidStr = uid.toStdString();
      bool success = false;
      auto storeResult = co_await imapClient->asyncUidStoreMail(uidStr, "+FLAGS (\\Deleted)");
      if (!storeResult.has_value())
      {
        qWarning() << "Failed to mark email as deleted:" << QString::fromStdString(storeResult.error().toString());
      }
      else
      {
        auto expungeResult = co_await imapClient->asyncUidExpunge(uidStr);
        if (!expungeResult.has_value())
        {
          qWarning() << "Failed to expunge email:" << QString::fromStdString(expungeResult.error().toString());
        }
        else
        {
          success = true;
        }
      }
      QMetaObject::invokeMethod(
          this,
          [this, uid, success]()
          {
            if (success)
            {
              // EXPUNGE invalidates the body cache entry for this UID
              // (it cannot be re-fetched and the UID will eventually be
              // re-used by the server for an unrelated message).
              m_messageCache.invalidate(emailMessageKey(uid));
              if (m_emailListManager)
              {
                m_emailListManager->removeEmail(uid);
              }
              resetEmailReader(QStringLiteral("Select an email to view"));
              showStatus("Email deleted");
            }
            else
            {
              showError("Delete Failed", "Failed to delete the email. Please try again.");
            }
          },
          Qt::BlockingQueuedConnection);
      break;
    }
    case ImapOpType::Move:
    {
      const QString uid = op.param1;
      const QString destinationMailbox = op.param2;
      std::string uidStr = uid.toStdString();
      std::string destMailbox = destinationMailbox.toStdString();

      QMetaObject::invokeMethod(
          this,
          [this, destinationMailbox]() { showStatus(QString("Moving to %1...").arg(destinationMailbox), 0); },
          Qt::BlockingQueuedConnection);

      auto moveResult = co_await imapClient->asyncUidMove(uidStr, destMailbox);
      if (moveResult.has_value())
      {
        QMetaObject::invokeMethod(
            this,
            [this, uid, destinationMailbox]()
            {
              // Source mailbox no longer holds this UID; destination
              // assigns a new UID under its own UIDVALIDITY, so the
              // body re-fetches naturally on next click. Drop the
              // source-side cached body.
              m_messageCache.invalidate(emailMessageKey(uid));
              if (m_emailListManager)
              {
                m_emailListManager->removeEmail(uid);
                // Destination mailbox listing is now stale.
                m_emailListManager->invalidateMailboxCache(destinationMailbox);
              }
              resetEmailReader(QStringLiteral("Select an email to view"));
              showStatus("Email moved successfully");
            },
            Qt::BlockingQueuedConnection);
      }
      else
      {
        auto copyResult = co_await imapClient->asyncUidCopyMail(uidStr, destMailbox);
        if (copyResult.has_value())
        {
          // Mark the source copy deleted, then expunge it. If either step
          // fails the message will appear in both mailboxes until the next
          // sync, so surface the error instead of silently discarding it.
          if (auto storeResult = co_await imapClient->asyncUidStoreMail(uidStr, "+FLAGS (\\Deleted)");
              !storeResult.has_value())
          {
            qWarning() << "IMAP STORE +FLAGS Deleted failed for UID" << uidStr.c_str() << ":"
                       << QString::fromStdString(storeResult.error().toString());
          }
          if (auto expungeResult = co_await imapClient->asyncUidExpunge(uidStr); !expungeResult.has_value())
          {
            qWarning() << "IMAP EXPUNGE failed for UID" << uidStr.c_str() << ":"
                       << QString::fromStdString(expungeResult.error().toString());
          }
          QMetaObject::invokeMethod(
              this,
              [this, uid, destinationMailbox]()
              {
                m_messageCache.invalidate(emailMessageKey(uid));
                if (m_emailListManager)
                {
                  m_emailListManager->removeEmail(uid);
                  m_emailListManager->invalidateMailboxCache(destinationMailbox);
                }
                resetEmailReader(QStringLiteral("Select an email to view"));
                showStatus("Email moved successfully");
              },
              Qt::BlockingQueuedConnection);
        }
        else
        {
          QMetaObject::invokeMethod(
              this, [this]() { showError("Move Failed", "Failed to move the email."); }, Qt::BlockingQueuedConnection);
        }
      }
      break;
    }
    case ImapOpType::ToggleFlag:
    {
      std::string uidStr = op.param1.toStdString();
      std::string flagStr = op.param2.toStdString();
      std::string flagAction = "+FLAGS (" + flagStr + ")";
      auto result = co_await imapClient->asyncUidStoreMail(uidStr, flagAction);
      if (!result.has_value())
      {
        qWarning() << "Failed to toggle flag:" << QString::fromStdString(result.error().toString());
      }
      break;
    }
  }

  co_return;
}

boost::asio::awaitable<void> MainWindow::fetchMailboxPageAwaitable(bool append, int serverMessageCountOrNeg1)
{
  auto imapClient = m_imapClient.lock();
  if (!imapClient)
  {
    co_return;
  }

  struct FetchCounts
  {
    int mailboxMessageCount = 0;
    int oldestSeq = 0;
  };
  auto counts = std::make_shared<FetchCounts>();
  QMetaObject::invokeMethod(
      this,
      [this, counts, serverMessageCountOrNeg1]()
      {
        if (serverMessageCountOrNeg1 >= 0)
        {
          counts->mailboxMessageCount = serverMessageCountOrNeg1;
        }
        else if (m_emailListManager)
        {
          counts->mailboxMessageCount = m_emailListManager->mailboxMessageCount();
          counts->oldestSeq = m_emailListManager->oldestLoadedSeqNum();
        }
      },
      Qt::BlockingQueuedConnection);
  int mailboxMessageCount = counts->mailboxMessageCount;
  int oldestSeq = counts->oldestSeq;

  if (!append)
  {
    QMetaObject::invokeMethod(this, [this]() { m_mailListScrollReadyForMore = true; }, Qt::BlockingQueuedConnection);
  }

  if (mailboxMessageCount == 0)
  {
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
          if (m_emailListManager)
          {
            m_emailListManager->applyFetchResults({}, false, 0, 0);
          }
          showStatus("No emails in this folder");
          if (!m_mailSession || !m_mailSession->idleLoopRunning())
          {
            startPolling();
          }
        },
        Qt::BlockingQueuedConnection);
    co_return;
  }

  int endSeq = append ? (oldestSeq - 1) : mailboxMessageCount;
  int startSeq = std::max(1, endSeq - EMAILS_PER_PAGE + 1);

  if (endSeq <= 0)
  {
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
          setUIEnabled(true);
          showStatus("No more emails to load");
          if (m_emailListManager)
          {
            m_emailListManager->refreshLoadMoreButton();
          }
        },
        Qt::BlockingQueuedConnection);
    co_return;
  }

  QMetaObject::invokeMethod(
      this,
      [this]()
      {
        setUIEnabled(false);
        showStatus("Loading emails...", 0);
      },
      Qt::BlockingQueuedConnection);

  QString seqRange = QString("%1:%2").arg(startSeq).arg(endSeq);
  std::string seqRangeStr = seqRange.toStdString();
  std::string response;
  try
  {
    auto result = co_await imapClient->asyncFetchMail(seqRangeStr, "(UID FLAGS ENVELOPE)");
    if (result.has_value())
    {
      response = std::move(result.value());
    }
    else
    {
      qWarning() << "IMAP FETCH failed:" << QString::fromStdString(result.error().toString());
    }
  }
  catch (const std::exception& e)
  {
    qWarning() << "IMAP FETCH exception:" << e.what();
  }

  QVector<EmailSummary> emails;
  if (!response.empty())
  {
    emails = EmailParser::parseEmailList(response);
    for (auto& summary : emails)
    {
      summary.subject = TextSanitizer::sanitizePlainText(summary.subject);
      summary.from = TextSanitizer::sanitizePlainText(summary.from);
      summary.preview = TextSanitizer::sanitizePlainText(summary.preview);
    }
  }

  QMetaObject::invokeMethod(
      this,
      [this, emails, append, startSeq]() { applyParsedEmailListOnQt(emails, append, startSeq); },
      Qt::BlockingQueuedConnection);

  co_return;
}

void MainWindow::checkForNewEmails()
{
  // With IDLE, we get push notifications - this is for manual refresh
  loadEmails();
}

void MainWindow::showStatus(const QString& message, int timeout)
{
  statusBar()->showMessage(message, timeout);
}

void MainWindow::showError(const QString& title, const QString& message)
{
  QMessageBox::critical(this, title, message);
}

void MainWindow::setUIEnabled(bool enabled)
{
  // OAuth button stays disabled if not configured even after re-enabling the UI.
  const bool oauthOk = OAuthCredentials::isConfigured();
  ui->LogInButton->setEnabled(enabled && oauthOk);
  ui->UsePasswordButton->setEnabled(enabled);
  ui->BackToOAuthButton->setEnabled(enabled);
  ui->PasswordSignInButton->setEnabled(enabled);
  ui->ComposeButton->setEnabled(enabled);
  ui->SendButton->setEnabled(enabled);
  ui->RefreshButton->setEnabled(enabled);
}

QPushButton* MainWindow::createFolderButton(const QString& folderName, const QString& displayName)
{
  QString label = displayName.isEmpty() ? folderName : displayName;
  auto* button = new QPushButton(label, this);
  button->setObjectName("NavButton");
  button->setProperty("folderName", folderName);
  button->setCheckable(true);
  button->setCursor(kInteractiveCursorShape);
  button->setMinimumHeight(40);

  connect(button, &QPushButton::clicked, this, [this, folderName]() { onFolderButtonClicked(folderName); });

  m_navButtonGroup->addButton(button);
  m_folderButtons.append(button);

  return button;
}

void MainWindow::applyParsedEmailListOnQt(const QVector<EmailSummary>& emails, bool append, int startSeq)
{
  if (emails.isEmpty())
  {
    setUIEnabled(true);
    if (!append)
    {
      qWarning() << "FETCH returned empty response";
      showStatus("Failed to load emails");
    }
    else
    {
      showStatus("No more emails to load");
      if (m_emailListManager != nullptr)
      {
        m_emailListManager->refreshLoadMoreButton();
      }
    }
    if (!m_mailSession || !m_mailSession->idleLoopRunning())
    {
      startPolling();
    }
    return;
  }

  int currentCount = (m_emailListManager != nullptr) ? m_emailListManager->mailboxMessageCount() : 0;
  if (m_emailListManager != nullptr)
  {
    m_emailListManager->applyFetchResults(emails, append, currentCount, startSeq);
  }

  setUIEnabled(true);
  int totalLoaded = (m_emailListManager != nullptr) ? m_emailListManager->loadedCount() : 0;
  showStatus(QString("Loaded %1 of %2 emails").arg(totalLoaded).arg(currentCount));

  if (!m_mailSession || !m_mailSession->idleLoopRunning())
  {
    startPolling();
  }
}

void MainWindow::applyParsedEmailBodyOnQt(const ParsedEmailContent& content, const QString& uid)
{
  const auto key = emailMessageKey(uid);

  // Always release the in-flight reservation we took in loadEmailContent(),
  // even on failure — otherwise a transient fetch error would block all future
  // clicks on this UID until the next folder switch.
  if (key.isValid())
  {
    m_messageCache.releasePending(key);
  }

  // Stale-response gate: when the user has already clicked away to a newer
  // message, m_pendingDisplayKey points at that newer key. We MUST NOT touch
  // the reader pane in that case (it would visibly snap back to an older
  // message, or to "Failed to load email" if this older fetch happened to
  // fail), but we still want to populate the cache so the next click on this
  // UID is instant.
  const bool isStale = m_pendingDisplayKey.isValid() && key.isValid() && key != m_pendingDisplayKey;

  if (!content.isValid())
  {
    if (isStale)
    {
      // Older fetch failed but the user is no longer waiting for it. Stay
      // quiet — the still-pending request will drive the UI on its own.
      return;
    }
    // Reset the entire reader pane (subject/meta/body/attachments) so a stale
    // attachments bar or body from a previously viewed message can't bleed
    // through behind the failure notice.
    resetEmailReader(QStringLiteral("Failed to load email"));
    return;
  }

  // Cache the body regardless of staleness, so a subsequent click on this UID
  // is served from tier 1 instead of going back to the network.
  if (key.isValid())
  {
    aurora::mail::app::cache::CachedMessage entry;
    entry.key = key;
    entry.content = content;
    entry.cachedAt = QDateTime::currentDateTime();
    entry.approximateBytes = aurora::mail::app::cache::CachedMessage::approximateBytesOf(content);
    m_messageCache.put(std::move(entry));
  }

  if (isStale)
  {
    // Body cached above; UI stays focused on what the user is actually
    // waiting for. Read state is also intentionally NOT updated here — the
    // user did not really "read" this message.
    return;
  }

  m_displayedMessageKey = key;

  ui->EmailSubjectLabel->setText(TextSanitizer::sanitizePlainText(content.subject));
  ui->EmailMetaLabel->setText(QString("From: %1").arg(TextSanitizer::sanitizePlainText(content.from)));

  QString sanitizedBody = TextSanitizer::sanitizeEmailBody(content.body);
  ui->EmailBodyText->setHtml(sanitizedBody);

  m_currentEmailAttachments = content.attachments;
  displayAttachments();
  showStatus("Email loaded");

  markEmailAsRead(uid);
}
