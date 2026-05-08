#ifndef ACCOUNT_REGISTRY_HPP
#define ACCOUNT_REGISTRY_HPP

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <optional>
#include <vector>

/**
 * @file AccountRegistry.hpp
 * @brief Non-secret registry of mail accounts known to this installation.
 *
 * Tracks the set of email accounts the user has signed into, the auth method
 * used (so we know which credential store to consult), the per-account server
 * hints (so we can reconnect without re-entering them), and the email of the
 * currently active account.
 *
 * @par Why QSettings (not the secure backend)
 * The data persisted here is intentionally non-secret: account identifiers,
 * auth method enum, IMAP/SMTP host:port. All actual secrets (OAuth tokens and
 * passwords) live in the secure backend behind their own per-account keys; an
 * attacker reading this registry learns only which accounts exist, not how to
 * authenticate as them.
 *
 * @par Threading
 * Intended to be called from the Qt thread that owns @c MainWindow. Not
 * thread-safe; QSettings I/O happens synchronously on every mutation.
 */
class AccountRegistry
{
 public:
  /// Auth method used to establish the account session.
  /// Mirrors @c EAuthMethod in MainWindow.hpp; kept independent so this
  /// header has no UI dependency.
  enum class AuthMethod : uint8_t
  {
    OAuth = 1,
    Password = 2
  };

  /// Persistent description of one registered account (no secrets).
  struct Account
  {
    QString email;
    AuthMethod method{ AuthMethod::OAuth };
    // Server hints (informational; password accounts also keep a copy in
    // PasswordCredentialsStorage's secure blob).
    QString imapServer{ QStringLiteral("imap.gmail.com") };
    quint16 imapPort{ 993 };
    QString smtpServer{ QStringLiteral("smtp.gmail.com") };
    quint16 smtpPort{ 587 };
  };

  explicit AccountRegistry(
      const QString& organization = QStringLiteral("AuroraMail"),
      const QString& application = QStringLiteral("AuroraMail-Accounts"))
      : settings_(organization, application)
  {
  }

  /// Returns the full list of registered accounts (insertion order preserved).
  [[nodiscard]] std::vector<Account> accounts() const
  {
    return loadAll();
  }

  /// Returns the email of the active account, or empty if none.
  [[nodiscard]] QString activeEmail() const
  {
    return settings_.value(QStringLiteral("activeEmail")).toString();
  }

  /// Sets (or clears, with empty string) the active account.
  void setActiveEmail(const QString& email)
  {
    if (email.isEmpty())
    {
      settings_.remove(QStringLiteral("activeEmail"));
    }
    else
    {
      settings_.setValue(QStringLiteral("activeEmail"), email);
    }
  }

  /// Returns the active account, if any.
  [[nodiscard]] std::optional<Account> activeAccount() const
  {
    const QString email = activeEmail();
    if (email.isEmpty())
    {
      return std::nullopt;
    }
    return findByEmail(email);
  }

  /// Returns true if an account with this email is registered.
  [[nodiscard]] bool contains(const QString& email) const
  {
    return findByEmail(email).has_value();
  }

  /// Returns the matching account, if any.
  [[nodiscard]] std::optional<Account> findByEmail(const QString& email) const
  {
    if (email.isEmpty())
    {
      return std::nullopt;
    }
    const auto all = loadAll();
    const auto it = std::find_if(all.begin(), all.end(), [&email](const Account& a) { return a.email == email; });
    if (it == all.end())
    {
      return std::nullopt;
    }
    return *it;
  }

  /**
   * @brief Inserts a new account or updates an existing one (matched by email).
   *
   * Does not change the active account. Use @c setActiveEmail() afterwards.
   */
  void upsert(const Account& account)
  {
    if (account.email.isEmpty())
    {
      return;
    }
    auto all = loadAll();
    auto it = std::find_if(all.begin(), all.end(), [&account](const Account& a) { return a.email == account.email; });
    if (it == all.end())
    {
      all.push_back(account);
    }
    else
    {
      *it = account;
    }
    saveAll(all);
  }

  /**
   * @brief Removes the account with the given email (if any).
   *
   * If the removed account was active, the active pointer is cleared. The
   * caller is responsible for purging the corresponding token / password
   * entries from the secure backend.
   */
  void remove(const QString& email)
  {
    if (email.isEmpty())
    {
      return;
    }
    auto all = loadAll();
    const auto newEnd = std::remove_if(all.begin(), all.end(), [&email](const Account& a) { return a.email == email; });
    if (newEnd == all.end())
    {
      return;
    }
    all.erase(newEnd, all.end());
    saveAll(all);

    if (activeEmail() == email)
    {
      setActiveEmail(QString());
    }
  }

  /// True if no accounts are registered.
  [[nodiscard]] bool isEmpty() const
  {
    return loadAll().empty();
  }

  /// Number of registered accounts.
  [[nodiscard]] std::size_t size() const
  {
    return loadAll().size();
  }

 private:
  static QString methodToString(AuthMethod m)
  {
    switch (m)
    {
      case AuthMethod::OAuth: return QStringLiteral("oauth");
      case AuthMethod::Password: return QStringLiteral("password");
    }
    return QStringLiteral("oauth");
  }

  static AuthMethod methodFromString(const QString& s)
  {
    if (s.compare(QStringLiteral("password"), Qt::CaseInsensitive) == 0)
    {
      return AuthMethod::Password;
    }
    return AuthMethod::OAuth;
  }

  [[nodiscard]] std::vector<Account> loadAll() const
  {
    std::vector<Account> out;
    const QByteArray raw = settings_.value(QStringLiteral("accounts")).toString().toUtf8();
    if (raw.isEmpty())
    {
      return out;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
      return out;
    }
    const QJsonArray arr = doc.array();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (const auto v : arr)
    {
      if (!v.isObject())
      {
        continue;
      }
      const QJsonObject o = v.toObject();
      Account a;
      a.email = o.value(QLatin1String("email")).toString();
      if (a.email.isEmpty())
      {
        continue;
      }
      a.method = methodFromString(o.value(QLatin1String("method")).toString());
      a.imapServer = o.value(QLatin1String("imapServer")).toString(QStringLiteral("imap.gmail.com"));
      a.imapPort = static_cast<quint16>(o.value(QLatin1String("imapPort")).toInt(993));
      a.smtpServer = o.value(QLatin1String("smtpServer")).toString(QStringLiteral("smtp.gmail.com"));
      a.smtpPort = static_cast<quint16>(o.value(QLatin1String("smtpPort")).toInt(587));
      out.push_back(std::move(a));
    }
    return out;
  }

  void saveAll(const std::vector<Account>& accounts)
  {
    QJsonArray arr;
    for (const auto& a : accounts)
    {
      QJsonObject o;
      o[QLatin1String("email")] = a.email;
      o[QLatin1String("method")] = methodToString(a.method);
      o[QLatin1String("imapServer")] = a.imapServer;
      o[QLatin1String("imapPort")] = a.imapPort;
      o[QLatin1String("smtpServer")] = a.smtpServer;
      o[QLatin1String("smtpPort")] = a.smtpPort;
      arr.append(o);
    }
    const QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    settings_.setValue(QStringLiteral("accounts"), json);
  }

  mutable QSettings settings_;
};

#endif  // ACCOUNT_REGISTRY_HPP
