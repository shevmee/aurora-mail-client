#ifndef MAIL_SESSION_SERVICE_HPP
#define MAIL_SESSION_SERVICE_HPP

#include <boost/asio.hpp>
#include <memory>

#include "ImapClient.hpp"
#include "ImapSessionController.hpp"
#include "ImapSessionTypes.hpp"

/**
 * Owns serialized IMAP session plumbing: strand, ImapSessionController (IDLE loop,
 * command pump, ImapCommandQueue), and exposes a single entry point for UI code.
 *
 * Application-specific command handling stays in the DispatchFn (typically implemented
 * on the main window as co_await to ImapClient + Qt callbacks).
 */
class MailSessionService
{
 public:
  using DispatchFn = ImapSessionController::DispatchFn;
  using Strand = ImapSessionController::Strand;

  MailSessionService(
      boost::asio::io_context& ioc,
      std::weak_ptr<aurora::mail::imap::ImapClient> imapClient,
      ImapSessionCallbacks callbacks,
      DispatchFn dispatch);

  MailSessionService(const MailSessionService&) = delete;
  MailSessionService& operator=(const MailSessionService&) = delete;
  MailSessionService(MailSessionService&&) = delete;
  MailSessionService& operator=(MailSessionService&&) = delete;

  void enqueueOperation(ImapOperation op);

  void startPolling();
  void requestStopIdleLoop();
  void resumeIdle();

  [[nodiscard]] bool idleLoopRunning() const;
  [[nodiscard]] bool imapBusy() const;
  [[nodiscard]] ImapConnectionPhase connectionPhase() const;

  /** All IMAP traffic (including login LIST) must be scheduled through this executor (strand). */
  [[nodiscard]] std::shared_ptr<Strand> imapStrand() const
  {
    return strand_;
  }

 private:
  std::shared_ptr<Strand> strand_;
  std::unique_ptr<ImapSessionController> controller_;
};

#endif
