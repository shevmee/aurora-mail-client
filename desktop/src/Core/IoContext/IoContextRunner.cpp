#include "IoContextRunner.hpp"

IoContextRunner::IoContextRunner(int concurrency_hint)
    : io_context_(std::make_unique<asio::io_context>(concurrency_hint))
    , work_guard_(asio::make_work_guard(*io_context_))
    , thread_([this] { io_context_->run(); })
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
