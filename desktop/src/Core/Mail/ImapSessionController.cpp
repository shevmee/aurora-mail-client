#include "ImapSessionController.hpp"

#include <QDebug>
#include <QString>
#include <chrono>

namespace
{
  void safeNotify(const std::function<void()>& fn)
  {
    if (fn)
    {
      fn();
    }
  }

  void safeNotifyStr(const std::function<void(const QString&)>& fn, const QString& s)
  {
    if (fn)
    {
      fn(s);
    }
  }
}  // namespace

ImapSessionController::ImapSessionController(
    boost::asio::io_context& ioc,
    std::shared_ptr<Strand> strand,
    std::weak_ptr<aurora::mail::imap::ImapClient> imapClient,
    ImapSessionCallbacks callbacks)
    : io_(ioc),
      strand_(std::move(strand)),
      imapClient_(std::move(imapClient)),
      callbacks_(std::move(callbacks))
{
}

void ImapSessionController::setDispatch(DispatchFn dispatch)
{
  dispatch_ = std::move(dispatch);
}

void ImapSessionController::startPolling()
{
  auto imapClient = imapClient_.lock();
  if (!imapClient || !strand_)
  {
    return;
  }

  if (m_idleRunning.exchange(true))
  {
    qDebug() << "IDLE already running";
    return;
  }

  m_stopIdle = false;
  m_commandPending = false;
  m_idleExited = true;
  setPhase(ImapConnectionPhase::Ready);

  qDebug() << "Starting IMAP IDLE for real-time notifications";

  boost::asio::co_spawn(
      *strand_,
      [this, imapClient]() -> boost::asio::awaitable<void>
      {
        auto executor = co_await boost::asio::this_coro::executor;
        boost::asio::steady_timer sleep_timer(executor);

        while (!m_stopIdle)
        {
          co_await boost::asio::post(executor, boost::asio::use_awaitable);

          if (m_commandPending)
          {
            sleep_timer.expires_after(std::chrono::milliseconds(50));
            co_await sleep_timer.async_wait(boost::asio::use_awaitable);
            continue;
          }

          m_idleExited = false;

          if (m_commandPending)
          {
            m_idleExited = true;
            setPhase(ImapConnectionPhase::Ready);
            continue;
          }

          auto startResult = co_await imapClient->asyncIdleStart();
          if (!startResult.has_value())
          {
            m_idleExited = true;
            setPhase(ImapConnectionPhase::Ready);
            qWarning() << "Failed to start IDLE:" << QString::fromStdString(startResult.error().toString());
            sleep_timer.expires_after(std::chrono::seconds(5));
            co_await sleep_timer.async_wait(boost::asio::use_awaitable);
            continue;
          }

          // Tight window: if a notification handler called enqueueOperation between the
          // two earlier m_commandPending checks and the asyncIdleStart completion, the
          // pump's cancelIdleWait was a no-op (idle_cancel_outstanding_ was nullptr — the
          // previous wait reset it, this wait has not yet installed its own signal).
          // Without this check we would proceed to asyncIdleWait, install a fresh signal,
          // and wait for up to kIdleWaitSec (5 min) with no way for the pump to break us
          // out, while the pump itself bails on waitIdleReleasedAsync after ~5s and drops
          // the queued op. Send DONE now and let the pump drain.
          if (m_commandPending.load())
          {
            auto doneResult = co_await imapClient->asyncIdleDone();
            m_idleExited = true;
            setPhase(ImapConnectionPhase::Ready);
            if (!doneResult.has_value())
            {
              qWarning() << "Failed to exit IDLE (post-start race):"
                         << QString::fromStdString(doneResult.error().toString());
              break;
            }
            continue;
          }

          setPhase(ImapConnectionPhase::ServerIdling);

          auto waitResult = co_await imapClient->asyncIdleWait(kIdleWaitSec);

          setPhase(ImapConnectionPhase::WaitingIdleExit);
          auto doneResult = co_await imapClient->asyncIdleDone();

          m_idleExited = true;
          setPhase(ImapConnectionPhase::Ready);
          qDebug() << "IDLE: Exited";

          if (m_commandPending.load())
          {
            scheduleImapPump();
          }

          if (!doneResult.has_value())
          {
            qWarning() << "Failed to exit IDLE cleanly:" << QString::fromStdString(doneResult.error().toString());
            break;
          }

          if (waitResult.has_value())
          {
            const QString notification = QString::fromStdString(waitResult.value());
            qDebug() << "IDLE: Notification received:" << notification;
            safeNotifyStr(callbacks_.onIdleRawNotification, notification);
          }
        }

        m_idleRunning = false;
        m_idleExited = true;
        setPhase(ImapConnectionPhase::Disconnected);
        qDebug() << "IDLE: Loop stopped";
        safeNotify(callbacks_.onIdleLoopFinished);
        co_return;
      },
      boost::asio::detached);
}

void ImapSessionController::requestStopIdleLoop()
{
  qDebug() << "Stopping IMAP IDLE";
  m_stopIdle = true;
  m_commandPending = true;

  if (auto imapClient = imapClient_.lock())
  {
    imapClient->cancelIdleWait();
  }

  if (!m_idleRunning.load())
  {
    return;
  }
}

void ImapSessionController::resumeIdle()
{
  if (!m_commandPending)
  {
    return;
  }
  if (!m_idleExited.load())
  {
    return;
  }
  qDebug() << "IDLE: Resuming...";
  m_commandPending = false;
}

void ImapSessionController::enqueueOperation(ImapOperation op)
{
  // Close the IDLE re-arm window synchronously, BEFORE the pump coroutine runs.
  //
  // Race we are fixing: when an IDLE push (e.g., "* N EXISTS") fires, the IDLE coroutine
  // wakes asyncIdleWait, runs asyncIdleDone, sets m_idleExited=true, logs "IDLE: Exited",
  // and immediately loops back to iter N+1 on the SAME strand. Meanwhile, the unsolicited
  // callback is marshalled to the Qt thread, which eventually calls enqueueOperation here.
  //
  // If we set m_commandPending only inside the pump's waitIdleReleasedAsync (called later,
  // once scheduleImapPump's posted task drains), iter N+1 has already passed its top-of-loop
  // check, flipped m_idleExited back to false, and called asyncIdleStart. cancelIdleWait
  // then emits on idle_cancel_outstanding_ — which is either nullptr (post asyncIdleWait
  // reset) or the fresh signal that iter N+1 has not yet installed. The cancel is lost,
  // waitIdleReleasedAsync polls for ~kIdlePauseTimeoutMs and bails with "timed out", and
  // the just-enqueued op is left in the queue with no consumer.
  //
  // Setting m_commandPending=true here (before queue_.enqueue, before scheduleImapPump's
  // post unwinds) makes the IDLE coroutine's "if (m_commandPending) { sleep 50ms; continue; }"
  // guard fire, which prevents iter N+1 from re-arming. The pump then sees m_idleExited
  // already true and proceeds without waiting.
  //
  // We also kick cancelIdleWait here so that if the IDLE coroutine is genuinely inside
  // asyncIdleWait (long-poll), it gets cancelled now rather than only when the pump
  // gets around to it. This matters when the operation is enqueued for a reason OTHER
  // than an IDLE push (e.g., user-initiated SelectMailbox click while IDLE is mid-wait).
  m_commandPending = true;
  if (m_idleRunning.load() && !m_idleExited.load())
  {
    if (auto imapClient = imapClient_.lock())
    {
      imapClient->cancelIdleWait();
    }
  }

  queue_.enqueue(std::move(op));
  scheduleImapPump();
}

void ImapSessionController::scheduleImapPump()
{
  if (!strand_)
  {
    return;
  }

  boost::asio::post(
      *strand_,
      [this]()
      {
        if (m_imapPumpSessionActive.exchange(true))
        {
          m_imapPumpRescheduleRequested.store(true);
          return;
        }

        boost::asio::co_spawn(
            *strand_,
            [this]() -> boost::asio::awaitable<void>
            {
              do
              {
                m_imapPumpRescheduleRequested.store(false);
                co_await runImapPumpSession();
              } while (m_imapPumpRescheduleRequested.exchange(false));

              m_imapPumpSessionActive.store(false);
              co_return;
            },
            boost::asio::detached);
      });
}

boost::asio::awaitable<bool> ImapSessionController::waitIdleReleasedAsync()
{
  if (!m_idleRunning.load())
  {
    co_return true;
  }

  // m_commandPending and cancelIdleWait are already set in enqueueOperation. Here we
  // only need to wait for the IDLE coroutine to acknowledge by setting m_idleExited=true
  // (either because asyncIdleWait returned with a push, or because cancelIdleWait broke
  // it out of long-poll). After enqueueOperation's synchronous pre-flag, the IDLE
  // coroutine's top-of-loop "if (m_commandPending) continue;" prevents it from re-arming
  // a fresh asyncIdleStart while we are waiting.
  //
  // Defensive: re-emit cancel here in case enqueueOperation didn't (e.g., m_idleRunning
  // was momentarily false between iterations, but iter N has since re-armed). One extra
  // cancel emit on a stale signal is a no-op; missing one would re-introduce the timeout.
  if (auto imapClient = imapClient_.lock())
  {
    imapClient->cancelIdleWait();
  }

  auto executor = co_await boost::asio::this_coro::executor;
  boost::asio::steady_timer poll_timer(executor);
  co_await boost::asio::post(executor, boost::asio::use_awaitable);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kIdlePauseTimeoutMs);

  while (m_idleRunning.load() && !m_idleExited.load())
  {
    if (std::chrono::steady_clock::now() > deadline)
    {
      qWarning() << "IDLE: waitIdleReleasedAsync timed out";
      co_return false;
    }
    poll_timer.expires_after(std::chrono::milliseconds(5));
    co_await poll_timer.async_wait(boost::asio::use_awaitable);
    co_await boost::asio::post(executor, boost::asio::use_awaitable);
  }

  co_return true;
}

boost::asio::awaitable<void> ImapSessionController::runImapPumpSession()
{
  if (!dispatch_)
  {
    qWarning() << "ImapSessionController: dispatch not set";
    co_return;
  }

  for (;;)
  {
    const bool idleOk = co_await waitIdleReleasedAsync();

    if (queue_.empty())
    {
      if (idleOk)
      {
        safeNotify(callbacks_.onPumpQueueDrainedResumeIdle);
      }
      co_return;
    }
    if (!idleOk)
    {
      safeNotify(callbacks_.onPumpIdleWaitTimeout);
      co_return;
    }

    ImapOperation op{};
    if (!queue_.tryPop(op))
    {
      continue;
    }

    m_imapBusy.store(true);
    setPhase(ImapConnectionPhase::ExecutingCommand);
    co_await dispatch_(op);
    m_imapBusy.store(false);
    setPhase(ImapConnectionPhase::Ready);
  }
}
