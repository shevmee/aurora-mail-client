#ifndef EMAILLISTMANAGER_HPP
#define EMAILLISTMANAGER_HPP

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>
#include <cstdint>

#include "Email/EmailParser.hpp"

class QWidget;
class QVBoxLayout;
class QPushButton;
class EmailItemWidget;

class EmailListManager : public QObject
{
  Q_OBJECT

 public:
  struct MailboxCacheEntry
  {
    QList<aurora::mail::app::email::EmailSummary> emails;
    uint64_t highestModSeq = 0;
    int messageCount = 0;
    bool valid = false;
  };

  explicit EmailListManager(
      QVBoxLayout* mailItemsLayout,
      QWidget* mailListContainer,
      QWidget* emailActionsBar,
      QObject* parent = nullptr);

  void setCurrentMailbox(const QString& mailbox);
  const QString& currentMailbox() const;

  void clearEmailList();
  void clearSelection();

  bool hasValidCache(const QString& mailbox) const;
  /** Drop cached list for a folder (e.g. after send adds a copy to Sent on the server). */
  void invalidateMailboxCache(const QString& mailbox);
  /** Drop every cached folder (e.g. on account switch — must not leak emails across accounts). */
  void invalidateAllCaches();
  void displayCachedEmails(const QString& mailbox);

  void applyFetchResults(
      const QVector<aurora::mail::app::email::EmailSummary>& emails,
      bool append,
      int mailboxMessageCount,
      int startSeq);

  void setMailboxMessageCount(int count);
  int mailboxMessageCount() const;

  void setOldestLoadedSeqNum(int seqNum);
  int oldestLoadedSeqNum() const;

  int loadedCount() const;
  void refreshLoadMoreButton();

  /** Older pages exist (sequence 1 .. oldest-1 not yet loaded). */
  bool canLoadMore() const;

  void setSearchFilter(const QString& text);

  QString selectedUid() const;
  QString selectedMessageId() const;

  const aurora::mail::app::email::EmailSummary* summaryForUid(const QString& uid) const;
  bool isEmailUnread(const QString& uid) const;
  void setEmailUnread(const QString& uid, bool unread);

  void removeEmail(const QString& uid);

 signals:
  void emailSelected(const QString& uid);
  void loadMoreRequested();

 private:
  void addEmailItem(const aurora::mail::app::email::EmailSummary& summary, bool atBottom);
  void updateLoadMoreButton();
  void updateCache(
      const QString& mailbox,
      const QVector<aurora::mail::app::email::EmailSummary>& emails,
      bool append,
      int mailboxMessageCount);
  void handleEmailItemClicked(const QString& uid);

  void ensureListBottomStretch();
  int indexBeforeBottomStretch() const;

  QVBoxLayout* m_mailItemsLayout = nullptr;
  QWidget* m_mailListContainer = nullptr;
  QWidget* m_emailActionsBar = nullptr;

  QMap<QString, EmailItemWidget*> m_emailItems;
  QString m_selectedEmailUid;
  QString m_selectedEmailMessageId;

  int m_mailboxMessageCount = 0;
  int m_oldestLoadedSeqNum = 0;
  QPushButton* m_loadMoreButton = nullptr;

  QString m_currentMailbox;
  QMap<QString, MailboxCacheEntry> m_mailboxCache;
};

#endif  // EMAILLISTMANAGER_HPP
