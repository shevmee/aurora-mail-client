#include "SessionCoordinator.hpp"

#include <QAction>
#include <QDebug>
#include <QMenu>

#include "MainWindow.hpp"  // for the full definition of EAuthMethod
#include "OAuthCredentials.hpp"

using aurora::mail::app::config::OAuthCredentials;

SessionCoordinator::SessionCoordinator(Bindings bindings, QObject* parent) : QObject(parent), m_b(bindings)
{
}

void SessionCoordinator::registerActiveAccount(EAuthMethod method)
{
  if (m_b.currentUser.isEmpty() || method == EAuthMethod::None)
  {
    return;
  }

  AccountRegistry::Account a;
  a.email = m_b.currentUser;
  a.method = (method == EAuthMethod::Password) ? AccountRegistry::AuthMethod::Password : AccountRegistry::AuthMethod::OAuth;
  a.imapServer = m_b.imapServer;
  a.imapPort = static_cast<quint16>(m_b.imapPort);
  a.smtpServer = m_b.smtpServer;
  a.smtpPort = static_cast<quint16>(m_b.smtpPort);

  m_b.accountRegistry.upsert(a);
  m_b.accountRegistry.setActiveEmail(m_b.currentUser);

  // Make sure subsequent OAuth token refreshes / saves go to this account's
  // per-account key (no-op for password sessions).
  if (method == EAuthMethod::OAuth)
  {
    m_b.oauthManager.setAccountIdentity(m_b.currentUser);
  }
}

bool SessionCoordinator::reconnectStoredAccount(const AccountRegistry::Account& account)
{
  if (account.email.isEmpty())
  {
    return false;
  }

  // Common identity prep regardless of auth method.
  m_b.imapServer = account.imapServer;
  m_b.imapPort = account.imapPort;
  m_b.smtpServer = account.smtpServer;
  m_b.smtpPort = account.smtpPort;
  m_b.currentUser = account.email;

  if (account.method == AccountRegistry::AuthMethod::OAuth)
  {
    // OAuth: re-target storage to this account, load its tokens, and
    // continue through the standard async authenticate path on the host.
    if (!OAuthCredentials::isConfigured())
    {
      qWarning() << "OAuth not configured; cannot reconnect OAuth account";
      return false;
    }
    m_b.oauthManager.setCredentials(OAuthCredentials::clientId(), OAuthCredentials::clientSecret());
    m_b.oauthManager.clearInMemorySession();
    m_b.oauthManager.setAccountIdentity(account.email);
    if (!m_b.oauthManager.loadStoredTokens())
    {
      qWarning() << "No stored OAuth tokens for account; cannot resume";
      return false;
    }
    emit oauthCompletionRequested();
    return true;
  }

  // Password account: pull the password from secure storage.
  auto stored = m_b.passwordCredentials.load(account.email);
  if (!stored)
  {
    qWarning() << "No stored password for account; cannot resume";
    return false;
  }

  // Mirror parsed values into the host's identity state used by the rest
  // of the UI.
  m_b.imapServer = stored->imapServer;
  m_b.imapPort = stored->imapPort;
  m_b.smtpServer = stored->smtpServer;
  m_b.smtpPort = stored->smtpPort;

  emit uiEnableRequested(false);
  emit statusMessageRequested(tr("Connecting to mail servers..."));
  emit passwordSignInRequested(*stored);
  return true;
}

void SessionCoordinator::populateAccountMenu(QMenu* menu)
{
  if (!menu)
  {
    return;
  }
  menu->clear();

  const auto accounts = m_b.accountRegistry.accounts();
  for (const auto& acc : accounts)
  {
    QAction* act = menu->addAction(acc.email);
    act->setCheckable(true);
    act->setChecked(acc.email == m_b.currentUser);
    const QString email = acc.email;
    connect(act, &QAction::triggered, this, [this, email]() { emit switchAccountRequested(email); });
  }

  if (!accounts.empty())
  {
    menu->addSeparator();
  }

  QAction* addAct = menu->addAction(tr("Add another account..."));
  connect(addAct, &QAction::triggered, this, [this]() { emit addAccountRequested(); });

  menu->addSeparator();
  QAction* signOutAct = menu->addAction(tr("Sign out of this account"));
  connect(signOutAct, &QAction::triggered, this, [this]() { emit signOutRequested(); });
}

bool SessionCoordinator::tryAutoResume()
{
  std::optional<AccountRegistry::Account> resumeTarget = m_b.accountRegistry.activeAccount();
  if (!resumeTarget)
  {
    const auto all = m_b.accountRegistry.accounts();
    if (!all.empty())
    {
      resumeTarget = all.front();
    }
  }
  if (resumeTarget)
  {
    if (reconnectStoredAccount(*resumeTarget))
    {
      emit statusMessageRequested(tr("Signing in..."));
      return true;
    }
    return false;
  }

  // Legacy migration path: no registered accounts, but the older
  // single-account token blob is still in storage.
  m_b.oauthManager.setAccountIdentity(QString());  // legacy key
  if (m_b.oauthManager.loadStoredTokens())
  {
    m_b.currentUser = m_b.oauthManager.getUserEmail();
    if (!m_b.currentUser.isEmpty())
    {
      // Re-key the storage to the per-account scheme and persist a
      // copy under the new key, so future loads don't depend on the
      // legacy fall-through.
      m_b.oauthManager.setAccountIdentity(m_b.currentUser);
      m_b.oauthManager.saveTokens();
      emit statusMessageRequested(tr("Signing in..."));
      emit oauthCompletionRequested();
      return true;
    }
  }
  return false;
}

SessionCoordinator::SignOutOutcome SessionCoordinator::signOutCurrentAccount()
{
  // Snapshot what we're signing out of BEFORE we mutate state, so we can
  // make the right decision about purging secrets vs. just unregistering.
  const QString signedOutEmail = m_b.currentUser;
  const EAuthMethod signedOutMethod = m_b.currentAuthMethod;

  // Ask the host to tear down the live network/UI session. The host slot
  // runs synchronously (Qt::DirectConnection in the same thread), so by
  // the time emit returns, identity has been reset via resetIdentity().
  emit sessionTeardownRequested();

  // Purge this account's secret. Sign-out is an explicit user action and
  // it would be surprising if the password / refresh-token survived it.
  if (signedOutMethod == EAuthMethod::OAuth && !signedOutEmail.isEmpty())
  {
    m_b.oauthManager.setAccountIdentity(signedOutEmail);
    m_b.oauthManager.signOut();
  }
  else if (signedOutMethod == EAuthMethod::Password && !signedOutEmail.isEmpty())
  {
    m_b.passwordCredentials.remove(signedOutEmail);
  }
  // For accounts other than the one we just signed out of, secrets remain
  // in the secure backend so the user can switch back to them.

  // Drop this account from the non-secret registry. Other accounts stay.
  if (!signedOutEmail.isEmpty())
  {
    m_b.accountRegistry.remove(signedOutEmail);
  }

  // If at least one other account remains, transparently switch to it
  // instead of forcing the user back through the login page. Picking the
  // first registered account is intentional and deterministic.
  const auto remaining = m_b.accountRegistry.accounts();
  if (!remaining.empty())
  {
    const auto& target = remaining.front();
    m_b.accountRegistry.setActiveEmail(target.email);
    if (reconnectStoredAccount(target))
    {
      return SignOutOutcome::SwitchedToAnotherAccount;
    }
    // Reconnect failed (e.g. revoked token) — fall through to login.
    m_b.accountRegistry.setActiveEmail(QString());
  }

  return SignOutOutcome::ShowLoginPage;
}

void SessionCoordinator::resetIdentity()
{
  m_b.currentUser.clear();
  m_b.currentAuthMethod = EAuthMethod::None;
  // Also clear the in-memory OAuth tokens so we don't accidentally use the
  // previous account's access token in the next session.
  m_b.oauthManager.clearInMemorySession();
}
