#ifndef SESSION_COORDINATOR_HPP
#define SESSION_COORDINATOR_HPP

#include <QObject>
#include <QString>
#include <cstdint>

#include "Auth/AccountRegistry.hpp"
#include "Auth/OAuthManager.hpp"
#include "Auth/PasswordCredentialsStorage.hpp"

class QMenu;

// Forward-declared with explicit underlying type so SessionCoordinator does not
// need to include MainWindow.hpp (avoiding a circular include).
enum class EAuthMethod : std::uint8_t;

/**
 * @class SessionCoordinator
 * @brief Façade for sign-in / multi-account / sign-out scenarios.
 *
 * Implements the "coordination" layer that was previously embedded in
 * MainWindow. It does not own any UI widget; instead it operates on the
 * application's identity state through references supplied via @ref Bindings
 * and signals UI-side effects back to the view.
 *
 * Splitting this layer out:
 *  * keeps MainWindow focused on widget plumbing and view assembly;
 *  * isolates orchestration in a class that can be unit-tested without a
 *    QMainWindow (only a tiny QObject test harness is required);
 *  * makes the scenario surface explicit and named, instead of being a
 *    collection of slots scattered across the window.
 */
class SessionCoordinator : public QObject
{
  Q_OBJECT

 public:
  /**
   * @struct Bindings
   * @brief Reference-bindings to the host's identity state.
   *
   * The host (MainWindow) keeps the actual storage of these fields; the
   * coordinator just reads/writes them via these references. This avoids a
   * cascade of renames at the call sites in the host, while still letting
   * the coordinator own the orchestration logic that mutates them.
   */
  struct Bindings
  {
    OAuthManager& oauthManager;
    PasswordCredentialsStorage& passwordCredentials;
    AccountRegistry& accountRegistry;

    QString& currentUser;
    EAuthMethod& currentAuthMethod;
    QString& imapServer;
    int& imapPort;
    QString& smtpServer;
    int& smtpPort;
  };

  /**
   * @enum SignOutOutcome
   * @brief Tells the host whether sign-out kicked off a switch to a
   *        remaining account or whether the login page should be shown.
   */
  enum class SignOutOutcome : std::uint8_t
  {
    SwitchedToAnotherAccount = 0,
    ShowLoginPage = 1
  };

  explicit SessionCoordinator(Bindings bindings, QObject* parent = nullptr);
  ~SessionCoordinator() override = default;

  SessionCoordinator(const SessionCoordinator&) = delete;
  SessionCoordinator& operator=(const SessionCoordinator&) = delete;

  /**
   * @brief Persists the just-authenticated session into the account registry.
   *
   * Adds (or refreshes) the account, marks it active, and re-targets the
   * OAuth token storage to that account email when applicable.
   */
  void registerActiveAccount(EAuthMethod method);

  /**
   * @brief Reconnects using stored credentials for @p account.
   *
   * Mirrors server hints into the host's identity state, then either kicks
   * off the OAuth completion path (via @ref oauthCompletionRequested) or
   * the password sign-in path (via @ref passwordSignInRequested).
   *
   * @return false when no usable secret is available (e.g. revoked refresh
   *         token, missing keychain entry); the caller should show the
   *         login page in that case.
   */
  bool reconnectStoredAccount(const AccountRegistry::Account& account);

  /**
   * @brief Rebuilds the popup menu attached to the sidebar account button.
   *
   * Listed: every registered account (with a check next to the active one),
   * a separator, "Add another account..." and "Sign out".
   */
  void populateAccountMenu(QMenu* menu);

  /**
   * @brief Attempts to silently resume the previously-active account.
   *
   * Honours, in order: registry's active account, first registered account,
   * the legacy single-account token blob (migrated to per-account keying on
   * success).
   *
   * @return true when a resume path is in progress (the host should display
   *         a "Signing in..." status and stay on the login page until
   *         authentication completes).
   */
  bool tryAutoResume();

  /**
   * @brief Drives the full sign-out scenario for the currently active account.
   *
   * Steps: ask the host to tear down the live network/UI session, purge
   * this account's secret (OAuth refresh token or stored password), drop
   * the entry from the non-secret registry, and either hand off to a
   * remaining account or signal the host to return to the login page.
   */
  SignOutOutcome signOutCurrentAccount();

  /**
   * @brief Resets in-memory identity / OAuth session state.
   *
   * Called by the host as part of @c teardownActiveSession after the live
   * network/UI session has been torn down.
   */
  void resetIdentity();

 signals:
  /// Show "Connecting to mail servers..." (or similar) on the status label.
  void statusMessageRequested(QString message);

  /// Toggle interactive UI affordances (typically @c setUIEnabled).
  void uiEnableRequested(bool enabled);

  /// Display a modal error dialog.
  void errorMessageRequested(QString title, QString message);

  /// Hand off to the host's password sign-in coroutine.
  void passwordSignInRequested(PasswordCredentialsStorage::Credentials creds);

  /**
   * @brief Continue the OAuth post-callback path.
   *
   * The host connects this to its OAuth-completion slot; the coordinator
   * uses a queued connection so the slot runs once the current call stack
   * unwinds (matching the @c QTimer::singleShot(0, ...) pattern from the
   * original code).
   */
  void oauthCompletionRequested();

  /// Tear down the live IMAP/SMTP session and reset per-mailbox UI state.
  void sessionTeardownRequested();

  /// Switch the main pages stack back to the login page (chooser sub-page).
  void loginPageRequested();

  /// User picked an account in the popup menu.
  void switchAccountRequested(QString email);

  /// User picked "Add another account...".
  void addAccountRequested();

  /// User picked "Sign out of this account".
  void signOutRequested();

 private:
  Bindings m_b;
};

#endif  // SESSION_COORDINATOR_HPP
