#ifndef MAIL_SESSION_SIGNALS_HPP
#define MAIL_SESSION_SIGNALS_HPP

#include <QObject>

#include "ImapSessionTypes.hpp"

/**
 * QObject bridge for mail session events — MainWindow can stay mostly view + connections.
 */
class MailSessionSignals : public QObject
{
  Q_OBJECT

 public:
  explicit MailSessionSignals(QObject* parent = nullptr) : QObject(parent)
  {
  }

  void publishLinkState(ImapSessionLinkState state)
  {
    emit imapLinkStateChanged(state);
  }

 signals:
  /** OAuth/IMAP/SMTP login flow: disconnected → connecting → authenticated (or back to disconnected). */
  void imapLinkStateChanged(ImapSessionLinkState state);
};

#endif
