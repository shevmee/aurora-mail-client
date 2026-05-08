#include "IoContextRunner.hpp"

#include <QDebug>
#include <exception>

IoContextRunner::IoContextRunner(int concurrency_hint)
    : io_context_(std::make_unique<asio::io_context>(concurrency_hint)),
      work_guard_(asio::make_work_guard(*io_context_)),
      thread_(
          [this]
          {
            // boost::asio::io_context::run() rethrows whatever a handler
            // (or a coroutine spawned with boost::asio::detached) lets
            // escape. If we leave the rethrow uncaught here, the asio
            // worker thread terminates with an unhandled exception, which
            // calls std::terminate() and immediately kills the entire
            // GUI process — no message box, no log line, the user just
            // sees the window vanish.
            //
            // Catch everything, log it, and resume the event loop. Per
            // asio docs, run() may be re-invoked after a rethrow without
            // calling restart() (the io_context is not in a stopped
            // state). Only break out when the destructor explicitly
            // stops the context.
            for (;;)
            {
              try
              {
                io_context_->run();
                return;  // Normal exit: work_guard was reset, queue drained.
              }
              catch (const std::exception& e)
              {
                qWarning() << "IoContextRunner: asio handler threw" << e.what()
                           << "— resuming io_context::run() to keep the app alive";
              }
              catch (...)
              {
                qWarning() << "IoContextRunner: asio handler threw a non-std exception"
                           << "— resuming io_context::run() to keep the app alive";
              }

              if (io_context_->stopped())
              {
                return;
              }
            }
          })
{
}

IoContextRunner::~IoContextRunner()
{
  work_guard_.reset();
  io_context_->stop();
  // jthread joins automatically
}

asio::io_context& IoContextRunner::get() noexcept
{
  return *io_context_;
}

const asio::io_context& IoContextRunner::get() const noexcept
{
  return *io_context_;
}

bool IoContextRunner::is_running() const noexcept
{
  return !io_context_->stopped();
}
