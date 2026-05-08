#ifndef IMAP_SESSION_CONTROLLER_HPP
#define IMAP_SESSION_CONTROLLER_HPP

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>

#include "ImapClient.hpp"
#include "ImapCommandQueue.hpp"
#include "ImapSessionTypes.hpp"

struct ImapSessionCallbacks
{
  /// Raw IDLE notification line(s) after leaving IDLE (for EXISTS / EXPUNGE / FETCH handling).
  std::function<void(const QString&)> onIdleRawNotification;
  /// Pump drained queue successfully; release command pending so IDLE can restart.
  std::function<void()> onPumpQueueDrainedResumeIdle;
  /// waitIdleReleasedAsync timed out; do not call onPumpQueueDrainedResumeIdle.
  std::function<void()> onPumpIdleWaitTimeout;
  /// IDLE coroutine exited (for stopPolling synchronization).
  std::function<void()> onIdleLoopFinished;
};

/**
 * Owns IMAP IDLE loop, command pump, wait/cancel coordination, and connection phase.
 * All network scheduling runs on the provided strand (serialized with respect to other strand work).
 */
class ImapSessionController
{
 public:
  using DispatchFn = std::function<boost::asio::awaitable<void>(const ImapOperation&)>;
  using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;

  ImapSessionController(
      boost::asio::io_context& ioc,
      std::shared_ptr<Strand> strand,
      std::weak_ptr<aurora::mail::imap::ImapClient> imapClient,
      ImapSessionCallbacks callbacks);

  void setDispatch(DispatchFn dispatch);

  void startPolling();
  void requestStopIdleLoop();

  void enqueueOperation(ImapOperation op);

  void resumeIdle();

  [[nodiscard]] bool idleLoopRunning() const noexcept
  {
    return m_idleRunning.load(std::memory_order_acquire);
  }

  [[nodiscard]] ImapConnectionPhase connectionPhase() const noexcept
  {
    return m_phase.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool imapBusy() const noexcept
  {
    return m_imapBusy.load(std::memory_order_acquire);
  }

  static constexpr int kIdleWaitSec = 300;
  /** Max time to wait for IDLE handshake/wait to release before pump aborts (large mailboxes / slow server). */
  static constexpr int kIdlePauseTimeoutMs = 30000;

 private:
  void scheduleImapPump();

  boost::asio::awaitable<bool> waitIdleReleasedAsync();
  boost::asio::awaitable<void> runImapPumpSession();

  void setPhase(ImapConnectionPhase p) noexcept
  {
    m_phase.store(p, std::memory_order_release);
  }

  boost::asio::io_context& io_;
  std::shared_ptr<Strand> strand_;
  std::weak_ptr<aurora::mail::imap::ImapClient> imapClient_;
  ImapSessionCallbacks callbacks_;
  DispatchFn dispatch_;

  ImapCommandQueue queue_;

  // Fast flags for IDLE/pump races; ImapConnectionPhase is the diagnostic snapshot (updated together).
  std::atomic<bool> m_idleRunning{ false };
  std::atomic<bool> m_stopIdle{ false };
  std::atomic<bool> m_commandPending{ false };
  std::atomic<bool> m_idleExited{ true };
  std::atomic<bool> m_imapBusy{ false };
  std::atomic<bool> m_imapPumpSessionActive{ false };
  std::atomic<bool> m_imapPumpRescheduleRequested{ false };
  std::atomic<ImapConnectionPhase> m_phase{ ImapConnectionPhase::Disconnected };
};

#endif
