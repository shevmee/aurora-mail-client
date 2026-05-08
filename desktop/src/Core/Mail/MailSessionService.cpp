#include "MailSessionService.hpp"

MailSessionService::MailSessionService(
    boost::asio::io_context& ioc,
    std::weak_ptr<aurora::mail::imap::ImapClient> imapClient,
    ImapSessionCallbacks callbacks,
    DispatchFn dispatch)
    : strand_(std::make_shared<Strand>(ioc.get_executor())),
      controller_(std::make_unique<ImapSessionController>(
          ioc,
          strand_,
          std::move(imapClient),
          std::move(callbacks)))
{
    controller_->setDispatch(std::move(dispatch));
}

void MailSessionService::enqueueOperation(ImapOperation op)
{
    controller_->enqueueOperation(std::move(op));
}

void MailSessionService::startPolling()
{
    controller_->startPolling();
}

void MailSessionService::requestStopIdleLoop()
{
    controller_->requestStopIdleLoop();
}

void MailSessionService::resumeIdle()
{
    controller_->resumeIdle();
}

bool MailSessionService::idleLoopRunning() const
{
    return controller_->idleLoopRunning();
}

bool MailSessionService::imapBusy() const
{
    return controller_->imapBusy();
}

ImapConnectionPhase MailSessionService::connectionPhase() const
{
    return controller_->connectionPhase();
}
