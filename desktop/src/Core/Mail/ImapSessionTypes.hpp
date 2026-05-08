#ifndef IMAP_SESSION_TYPES_HPP
#define IMAP_SESSION_TYPES_HPP

#include <QString>
#include <cstdint>

/**
 * High-level IMAP session phase for diagnostics and UI (single-writer in ImapSessionController).
 * Describes IDLE/pump/command overlap — orthogonal to TLS/auth (see ImapSessionLinkState).
 */
enum class ImapConnectionPhase : std::uint8_t
{
  Disconnected = 0,
  Ready,
  ServerIdling,
  WaitingIdleExit,
  ExecutingCommand
};

/**
 * Transport/session lifecycle for the account (login, logout). Updated from MainWindow mail flow.
 * ImapConnectionPhase covers post-auth IDLE+pump; this covers connect/auth/disconnect.
 */
enum class ImapSessionLinkState : std::uint8_t
{
  Disconnected = 0,
  Connecting,
  Authenticated
};

enum class ImapOpType
{
  SelectMailbox,
  LoadEmail,
  FetchMailboxPage,
  MarkRead,
  Delete,
  Move,
  ToggleFlag
};

struct ImapOperation
{
  ImapOpType type{};
  QString param1;
  QString param2;
  bool boolParam = true;
};

#endif
