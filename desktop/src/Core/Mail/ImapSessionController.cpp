#include "ImapSessionController.hpp"

#include <chrono>

#include <QDebug>
#include <QString>

namespace
{
void safeNotify(const std::function<void()>& fn)
{
    if (fn) {
        fn();
    }
}

void safeNotifyStr(const std::function<void(const QString&)>& fn, const QString& s)
{
    if (fn) {
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
    if (!imapClient || !strand_) {
        return;
    }

    if (m_idleRunning.exchange(true)) {
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
        [this, imapClient]() -> boost::asio::awaitable<void> {
            auto executor = co_await boost::asio::this_coro::executor;
            boost::asio::steady_timer sleep_timer(executor);

            while (!m_stopIdle) {
                co_await boost::asio::post(executor, boost::asio::use_awaitable);

                if (m_commandPending) {
                    sleep_timer.expires_after(std::chrono::milliseconds(50));
                    co_await sleep_timer.async_wait(boost::asio::use_awaitable);
                    continue;
                }

                m_idleExited = false;

                if (m_commandPending) {
                    m_idleExited = true;
                    setPhase(ImapConnectionPhase::Ready);
                    continue;
                }

                auto startResult = co_await imapClient->asyncIdleStart();
                if (!startResult.has_value()) {
                    m_idleExited = true;
                    setPhase(ImapConnectionPhase::Ready);
                    qWarning() << "Failed to start IDLE:"
                               << QString::fromStdString(startResult.error().toString());
                    sleep_timer.expires_after(std::chrono::seconds(5));
                    co_await sleep_timer.async_wait(boost::asio::use_awaitable);
                    continue;
                }

                setPhase(ImapConnectionPhase::ServerIdling);
                // Entered state is logged by ImapClient after "+ idling" (avoids qDebug/spdlog ordering confusion).

                auto waitResult = co_await imapClient->asyncIdleWait(kIdleWaitSec);

                setPhase(ImapConnectionPhase::WaitingIdleExit);
                auto doneResult = co_await imapClient->asyncIdleDone();

                m_idleExited = true;
                setPhase(ImapConnectionPhase::Ready);
                qDebug() << "IDLE: Exited";

                if (m_commandPending.load()) {
                    scheduleImapPump();
                }

                if (!doneResult.has_value()) {
                    qWarning() << "Failed to exit IDLE cleanly:"
                               << QString::fromStdString(doneResult.error().toString());
                    break;
                }

                if (waitResult.has_value()) {
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

    if (auto imapClient = imapClient_.lock()) {
        imapClient->cancelIdleWait();
    }

    if (!m_idleRunning.load()) {
        return;
    }
}

void ImapSessionController::resumeIdle()
{
    if (!m_commandPending) {
        return;
    }
    if (!m_idleExited.load()) {
        return;
    }
    qDebug() << "IDLE: Resuming...";
    m_commandPending = false;
}

void ImapSessionController::enqueueOperation(ImapOperation op)
{
    queue_.enqueue(std::move(op));
    scheduleImapPump();
}

void ImapSessionController::scheduleImapPump()
{
    if (!strand_) {
        return;
    }

    boost::asio::post(*strand_, [this]() {
        if (m_imapPumpSessionActive.exchange(true)) {
            m_imapPumpRescheduleRequested.store(true);
            return;
        }

        boost::asio::co_spawn(
            *strand_,
            [this]() -> boost::asio::awaitable<void> {
                do {
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
    if (!m_idleRunning.load()) {
        co_return true;
    }

    m_commandPending = true;
    if (auto imapClient = imapClient_.lock()) {
        imapClient->cancelIdleWait();
    }

    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::steady_timer poll_timer(executor);
    co_await boost::asio::post(executor, boost::asio::use_awaitable);
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(kIdlePauseTimeoutMs);

    while (m_idleRunning.load() && !m_idleExited.load()) {
        if (std::chrono::steady_clock::now() > deadline) {
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
    if (!dispatch_) {
        qWarning() << "ImapSessionController: dispatch not set";
        co_return;
    }

    for (;;) {
        const bool idleOk = co_await waitIdleReleasedAsync();

        if (queue_.empty()) {
            if (idleOk) {
                safeNotify(callbacks_.onPumpQueueDrainedResumeIdle);
            }
            co_return;
        }
        if (!idleOk) {
            safeNotify(callbacks_.onPumpIdleWaitTimeout);
            co_return;
        }

        ImapOperation op{};
        if (!queue_.tryPop(op)) {
            continue;
        }

        m_imapBusy.store(true);
        setPhase(ImapConnectionPhase::ExecutingCommand);
        co_await dispatch_(op);
        m_imapBusy.store(false);
        setPhase(ImapConnectionPhase::Ready);
    }
}
