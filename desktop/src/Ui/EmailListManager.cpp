#include "EmailListManager.hpp"

#include <QLayoutItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "Widgets/EmailItemWidget.hpp"

using aurora::mail::app::email::EmailSummary;

namespace
{

  /**
   * Heuristic check for an outgoing folder where the recipient (To) is the
   * meaningful identity to display in the list, rather than the sender (which
   * is the local user). Matches Gmail "[Gmail]/Sent Mail", IMAP "Sent",
   * "Sent Items" / "Sent Mail", and dotted hierarchies like "INBOX.Sent".
   *
   * Conservative: requires the LAST path segment to start with "Sent " or to
   * equal "Sent" exactly so we don't accidentally treat a user-named folder
   * such as "Sent receipts" the same way unless it actually is one. (We allow
   * "Sent Items" / "Sent Mail" via the prefix-with-space rule.)
   */
  bool mailboxIsOutgoing(const QString& mailbox)
  {
    if (mailbox.isEmpty())
    {
      return false;
    }
    const qsizetype slash = std::max(mailbox.lastIndexOf(QLatin1Char('/')), mailbox.lastIndexOf(QLatin1Char('.')));
    const QString tail = (slash >= 0) ? mailbox.mid(slash + 1) : mailbox;
    if (tail.compare(QStringLiteral("Sent"), Qt::CaseInsensitive) == 0)
    {
      return true;
    }
    if (tail.startsWith(QStringLiteral("Sent "), Qt::CaseInsensitive))
    {
      return true;
    }
    return false;
  }

}  // namespace

EmailListManager::EmailListManager(
    QVBoxLayout* mailItemsLayout,
    QWidget* mailListContainer,
    QWidget* emailActionsBar,
    QObject* parent)
    : QObject(parent),
      m_mailItemsLayout(mailItemsLayout),
      m_mailListContainer(mailListContainer),
      m_emailActionsBar(emailActionsBar)
{
  if (m_mailItemsLayout != nullptr)
  {
    m_mailItemsLayout->setAlignment(Qt::AlignTop);
    ensureListBottomStretch();
  }
}

void EmailListManager::ensureListBottomStretch()
{
  if (m_mailItemsLayout == nullptr)
  {
    return;
  }
  if (m_mailItemsLayout->count() > 0)
  {
    QLayoutItem* last = m_mailItemsLayout->itemAt(m_mailItemsLayout->count() - 1);
    if ((last != nullptr) && (last->spacerItem() != nullptr))
    {
      return;
    }
  }
  m_mailItemsLayout->addStretch(1);
}

int EmailListManager::indexBeforeBottomStretch() const
{
  if ((m_mailItemsLayout == nullptr) || m_mailItemsLayout->count() == 0)
  {
    return 0;
  }
  QLayoutItem* last = m_mailItemsLayout->itemAt(m_mailItemsLayout->count() - 1);
  if ((last != nullptr) && (last->spacerItem() != nullptr))
  {
    return m_mailItemsLayout->count() - 1;
  }
  return m_mailItemsLayout->count();
}

void EmailListManager::setCurrentMailbox(const QString& mailbox)
{
  m_currentMailbox = mailbox;
}

const QString& EmailListManager::currentMailbox() const
{
  return m_currentMailbox;
}

void EmailListManager::clearEmailList()
{
  if (m_mailItemsLayout == nullptr)
  {
    return;
  }

  QLayoutItem* item = nullptr;
  while ((item = m_mailItemsLayout->takeAt(0)) != nullptr)
  {
    if (item->widget() != nullptr && item->widget() != m_loadMoreButton)
    {
      item->widget()->deleteLater();
    }
    delete item;
  }

  m_emailItems.clear();

  if (m_loadMoreButton != nullptr)
  {
    m_loadMoreButton->setParent(nullptr);
    m_loadMoreButton->hide();
  }

  clearSelection();
  ensureListBottomStretch();
}

void EmailListManager::clearSelection()
{
  if (!m_selectedEmailUid.isEmpty())
  {
    if (auto* item = m_emailItems.value(m_selectedEmailUid, nullptr))
    {
      item->setSelected(false);
    }
  }

  m_selectedEmailUid.clear();
  m_selectedEmailMessageId.clear();

  if (m_emailActionsBar != nullptr)
  {
    m_emailActionsBar->hide();
  }
}

bool EmailListManager::hasValidCache(const QString& mailbox) const
{
  auto it = m_mailboxCache.find(mailbox);
  return it != m_mailboxCache.end() && it->valid;
}

void EmailListManager::invalidateMailboxCache(const QString& mailbox)
{
  m_mailboxCache.remove(mailbox);
}

void EmailListManager::invalidateAllCaches()
{
  // Drop every per-folder cache and reset bookkeeping. This is critical on
  // account switch: without it, the next account's first SELECT would race
  // against the prior account's cached "instant display" and briefly show
  // emails from the wrong inbox.
  m_mailboxCache.clear();
  m_mailboxMessageCount = 0;
  m_oldestLoadedSeqNum = 0;
}

void EmailListManager::displayCachedEmails(const QString& mailbox)
{
  if (!hasValidCache(mailbox))
  {
    return;
  }

  m_currentMailbox = mailbox;
  const auto& cache = m_mailboxCache[mailbox];

  clearEmailList();

  m_mailboxMessageCount = cache.messageCount;
  m_oldestLoadedSeqNum = std::max(1, m_mailboxMessageCount - static_cast<int>(cache.emails.size()) + 1);

  // cache.emails is stored newest-first (seq DESC). addEmailItem(false) prepends at index 0,
  // so we walk the cache from the OLDEST end and prepend each — the last prepend (newest)
  // ends up at the top of the visible list.
  for (auto it = cache.emails.crbegin(); it != cache.emails.crend(); ++it)
  {
    addEmailItem(*it, false);
  }

  updateLoadMoreButton();
}

void EmailListManager::applyFetchResults(
    const QVector<EmailSummary>& emails,
    bool append,
    int mailboxMessageCount,
    int startSeq)
{
  setMailboxMessageCount(mailboxMessageCount);
  updateCache(m_currentMailbox, emails, append, mailboxMessageCount);

  if (!append)
  {
    clearEmailList();
  }

  // IMAP returns FETCH responses in seq-ascending order, so `emails` is oldest-first.
  // - First page (!append): prepend in oldest→newest order. Each addEmailItem(false) does
  //   insertWidget(0), so the newest message ends up at the top of the list.
  // - Load-more (append): the new batch contains messages OLDER than what's already shown.
  //   Walk the batch newest-first and append at the bottom so the list stays continuously
  //   newest-on-top, oldest-on-bottom across pages.
  if (append)
  {
    for (qsizetype i = emails.size() - 1; i >= 0; --i)
    {
      addEmailItem(emails[i], true);
    }
  }
  else
  {
    for (qsizetype i = 0; i < emails.size(); ++i)
    {
      addEmailItem(emails[i], false);
    }
  }

  m_oldestLoadedSeqNum = startSeq;
  updateLoadMoreButton();
}

void EmailListManager::setMailboxMessageCount(int count)
{
  m_mailboxMessageCount = std::max(0, count);
}

int EmailListManager::mailboxMessageCount() const
{
  return m_mailboxMessageCount;
}

void EmailListManager::setOldestLoadedSeqNum(int seqNum)
{
  m_oldestLoadedSeqNum = seqNum;
}

int EmailListManager::oldestLoadedSeqNum() const
{
  return m_oldestLoadedSeqNum;
}

int EmailListManager::loadedCount() const
{
  return m_emailItems.size();
}

void EmailListManager::refreshLoadMoreButton()
{
  updateLoadMoreButton();
}

bool EmailListManager::canLoadMore() const
{
  return m_oldestLoadedSeqNum > 1;
}

void EmailListManager::setSearchFilter(const QString& text)
{
  QString searchLower = text.toLower();

  for (auto it = m_emailItems.begin(); it != m_emailItems.end(); ++it)
  {
    EmailItemWidget* item = it.value();
    const EmailSummary& summary = item->summary();

    bool matches = text.isEmpty() || summary.from.toLower().contains(searchLower) ||
                   summary.subject.toLower().contains(searchLower) || summary.preview.toLower().contains(searchLower);

    item->setVisible(matches);
  }
}

QString EmailListManager::selectedUid() const
{
  return m_selectedEmailUid;
}

QString EmailListManager::selectedMessageId() const
{
  return m_selectedEmailMessageId;
}

const EmailSummary* EmailListManager::summaryForUid(const QString& uid) const
{
  auto* item = m_emailItems.value(uid, nullptr);
  if (item == nullptr)
  {
    return nullptr;
  }
  return &item->summary();
}

bool EmailListManager::isEmailUnread(const QString& uid) const
{
  auto* item = m_emailItems.value(uid, nullptr);
  return (item != nullptr) ? item->isUnread() : false;
}

void EmailListManager::setEmailUnread(const QString& uid, bool unread)
{
  auto* item = m_emailItems.value(uid, nullptr);
  if (item != nullptr)
  {
    item->setUnread(unread);
  }
}

void EmailListManager::removeEmail(const QString& uid)
{
  if (m_emailItems.contains(uid))
  {
    m_emailItems[uid]->deleteLater();
    m_emailItems.remove(uid);
  }

  if (!m_currentMailbox.isEmpty() && m_mailboxCache.contains(m_currentMailbox))
  {
    auto& cache = m_mailboxCache[m_currentMailbox];
    cache.emails.erase(
        std::remove_if(cache.emails.begin(), cache.emails.end(), [&uid](const EmailSummary& e) { return e.uid == uid; }),
        cache.emails.end());
    if (cache.messageCount > 0)
    {
      cache.messageCount--;
    }
  }

  if (m_mailboxMessageCount > 0)
  {
    m_mailboxMessageCount--;
  }

  if (m_selectedEmailUid == uid)
  {
    clearSelection();
  }

  updateLoadMoreButton();
}

void EmailListManager::addEmailItem(const EmailSummary& summary, bool atBottom)
{
  if (m_mailItemsLayout == nullptr)
  {
    return;
  }

  QWidget* parentWidget = (m_mailListContainer != nullptr) ? m_mailListContainer : m_mailItemsLayout->parentWidget();
  auto* emailItem = new EmailItemWidget(summary, parentWidget);
  emailItem->setShowRecipient(mailboxIsOutgoing(m_currentMailbox));

  connect(emailItem, &EmailItemWidget::clicked, this, [this](const QString& uid) { handleEmailItemClicked(uid); });

  ensureListBottomStretch();

  if (atBottom)
  {
    int insertIndex = indexBeforeBottomStretch();
    if ((m_loadMoreButton != nullptr) && m_loadMoreButton->isVisible())
    {
      for (int i = 0; i < m_mailItemsLayout->count(); ++i)
      {
        QLayoutItem* it = m_mailItemsLayout->itemAt(i);
        if ((it != nullptr) && it->widget() == m_loadMoreButton)
        {
          insertIndex = i;
          break;
        }
      }
    }
    m_mailItemsLayout->insertWidget(insertIndex, emailItem);
  }
  else
  {
    m_mailItemsLayout->insertWidget(0, emailItem);
  }

  m_emailItems[summary.uid] = emailItem;
}

void EmailListManager::updateLoadMoreButton()
{
  if ((m_mailItemsLayout == nullptr) || (m_mailListContainer == nullptr))
  {
    return;
  }

  if (m_loadMoreButton == nullptr)
  {
    m_loadMoreButton = new QPushButton(tr("Load More Emails"), m_mailListContainer);
    m_loadMoreButton->setStyleSheet(R"(
            QPushButton {
                background-color: #3a4a5a;
                color: #e8e8e8;
                border: 1px solid #4a5a6a;
                border-radius: 6px;
                padding: 10px 20px;
                font-size: 14px;
                margin: 10px;
            }
            QPushButton:hover {
                background-color: #4a6a8a;
            }
            QPushButton:pressed {
                background-color: #2a3a4a;
            }
        )");
    connect(m_loadMoreButton, &QPushButton::clicked, this, &EmailListManager::loadMoreRequested);
  }

  bool hasMore = m_oldestLoadedSeqNum > 1;

  ensureListBottomStretch();

  if (hasMore)
  {
    m_loadMoreButton->setParent(m_mailListContainer);
    m_mailItemsLayout->removeWidget(m_loadMoreButton);
    int insertIdx = indexBeforeBottomStretch();
    m_mailItemsLayout->insertWidget(insertIdx, m_loadMoreButton);
    int remaining = m_oldestLoadedSeqNum - 1;
    m_loadMoreButton->setText(QString("Load More (%1 remaining)").arg(remaining));
    m_loadMoreButton->show();
  }
  else
  {
    m_mailItemsLayout->removeWidget(m_loadMoreButton);
    m_loadMoreButton->hide();
  }
}

void EmailListManager::updateCache(
    const QString& mailbox,
    const QVector<EmailSummary>& emails,
    bool append,
    int mailboxMessageCount)
{
  if (mailbox.isEmpty())
  {
    return;
  }

  auto& cache = m_mailboxCache[mailbox];
  if (!append)
  {
    cache.emails.clear();
  }

  for (qsizetype i = emails.size() - 1; i >= 0; --i)
  {
    cache.emails.append(emails[i]);
  }

  cache.messageCount = mailboxMessageCount;
  cache.valid = true;
}

void EmailListManager::handleEmailItemClicked(const QString& uid)
{
  if (!m_selectedEmailUid.isEmpty() && m_emailItems.contains(m_selectedEmailUid))
  {
    m_emailItems[m_selectedEmailUid]->setSelected(false);
  }

  m_selectedEmailUid = uid;
  if (m_emailItems.contains(uid))
  {
    m_emailItems[uid]->setSelected(true);
    m_emailItems[uid]->setUnread(false);
    m_selectedEmailMessageId = m_emailItems[uid]->summary().messageId;
  }

  if (m_emailActionsBar != nullptr)
  {
    m_emailActionsBar->show();
  }

  emit emailSelected(uid);
}
