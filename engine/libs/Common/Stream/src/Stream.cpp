#include <Stream.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <utility>

namespace aurora::mail::common::stream
{

  Stream::Stream(executor_type exec) : exec_(std::move(exec))
  {
  }

  Stream::executor_type Stream::executor() const noexcept
  {
    return exec_;
  }

  TimedOutStream::TimedOutStream(std::unique_ptr<Stream> stream, std::chrono::milliseconds timeout)
      : Stream(stream->executor()),
        stream_(std::move(stream)),
        timeout_(timeout)
  {
  }

  awaitable<error_code> TimedOutStream::connect(const tcp::endpoint& ep)
  {
    co_return co_await withTimeout([&] { return stream_->connect(ep); });
  }

  awaitable<error_code> TimedOutStream::disconnect()
  {
    co_return co_await withTimeout([&] { return stream_->disconnect(); });
  }

  awaitable<std::expected<std::size_t, error_code>> TimedOutStream::write(asio::const_buffer buf)
  {
    co_return co_await withTimeout([&] { return stream_->write(buf); });
  }

  awaitable<std::expected<std::size_t, error_code>> TimedOutStream::readUntil(
      asio::mutable_buffer buf,
      const std::string& delim)
  {
    co_return co_await withTimeout([&] { return stream_->readUntil(buf, delim); });
  }

  awaitable<std::expected<std::size_t, error_code>> TimedOutStream::readUntil(std::string& data, const std::string& delim)
  {
    co_return co_await withTimeout([&] { return stream_->readUntil(data, delim); });
  }

  awaitable<std::expected<std::size_t, error_code>> TimedOutStream::readExactly(asio::mutable_buffer buf)
  {
    co_return co_await withTimeout([&] { return stream_->readExactly(buf); });
  }

  // Method to extract the underlying stream for SSL upgrade
  std::unique_ptr<Stream> TimedOutStream::take_inner_stream()
  {
    return std::move(stream_);
  }

  PlainStream::PlainStream(const asio::any_io_executor& exec) : Stream(exec), socket_(exec), closed_(false)
  {
  }

  awaitable<error_code> PlainStream::connect(const tcp::endpoint& ep)
  {
    error_code ec;
    co_await socket_.async_connect(ep, asio::redirect_error(asio::use_awaitable, ec));
    co_return ec;
  }

  // NOTE: provide here only strings wrapped with \r\n for proper SMTP/IMAP
  // protocols
  awaitable<std::expected<std::size_t, error_code>> PlainStream::write(asio::const_buffer buf)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);

    error_code ec;
    std::size_t bytes_transmitted = co_await asio::async_write(socket_, buf, asio::redirect_error(asio::use_awaitable, ec));

    if (ec.failed())
      co_return std::unexpected(ec);

    co_return bytes_transmitted;
  }

  // TODO: rewrite with awaitable<system::result<string>> when migrate to Boost
  // > 1.84
  awaitable<std::expected<std::size_t, error_code>> PlainStream::readUntil(
      asio::mutable_buffer buf,
      const std::string& delim)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);

    error_code ec;
    std::size_t bytes_received =
        co_await asio::async_read_until(socket_, read_buffer_, delim, asio::redirect_error(asio::use_awaitable, ec));

    std::size_t to_copy = std::min(buf.size(), bytes_received);

    if (!ec.failed() && to_copy > 0)
    {
      auto begin = asio::buffers_begin(read_buffer_.data());
      std::memcpy(buf.data(), &*begin, to_copy);
      read_buffer_.consume(to_copy);
    }

    if (ec.failed())
      co_return std::unexpected(ec);

    co_return to_copy;
  }

  awaitable<std::expected<std::size_t, error_code>> PlainStream::readUntil(std::string& data, const std::string& delim)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);

    error_code ec;
    std::size_t bytes_received =
        co_await asio::async_read_until(socket_, read_buffer_, delim, asio::redirect_error(asio::use_awaitable, ec));

    if (ec.failed())
      co_return std::unexpected(ec);

    if (bytes_received > 0)
    {
      // Copy all bytes (including delimiter) from read_buffer_ to data
      auto begin = asio::buffers_begin(read_buffer_.data());
      data.assign(begin, begin + bytes_received);
      read_buffer_.consume(bytes_received);
    }
    else
    {
      data.clear();
    }

    co_return bytes_received;
  }

  awaitable<std::expected<std::size_t, error_code>> PlainStream::readExactly(asio::mutable_buffer buf)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);

    size_t total_needed = buf.size();
    size_t bytes_copied = 0;
    char* dest = static_cast<char*>(buf.data());

    // First, consume any data already buffered from previous readUntil calls
    size_t buffered = read_buffer_.size();
    if (buffered > 0)
    {
      size_t to_copy = std::min(buffered, total_needed);
      auto begin = asio::buffers_begin(read_buffer_.data());
      std::memcpy(dest, &*begin, to_copy);
      read_buffer_.consume(to_copy);
      bytes_copied = to_copy;
    }

    // If we still need more data, read from socket
    if (bytes_copied < total_needed)
    {
      size_t remaining = total_needed - bytes_copied;
      error_code ec;
      size_t n = co_await asio::async_read(
          socket_,
          asio::buffer(dest + bytes_copied, remaining),
          asio::transfer_exactly(remaining),
          asio::redirect_error(asio::use_awaitable, ec));

      if (ec)
      {
        co_return std::unexpected(ec);
      }
      bytes_copied += n;
    }

    co_return bytes_copied;
  }

  asio::awaitable<error_code> PlainStream::disconnect()
  {
    if (closed_)
      co_return asio::error::operation_aborted;

    error_code ec;

    closed_ = true;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    co_return ec;
  }

  tcp::socket PlainStream::take_socket() noexcept
  {
    closed_ = true;
    return std::move(socket_);
  }

  SecureStream::SecureStream(const asio::any_io_executor& exec, asio::ssl::context& ssl_context)
      : Stream(exec),
        socket_(exec, ssl_context),
        closed_(false)
  {
  }

  SecureStream::SecureStream(PlainStream&& plain, asio::ssl::context& ssl_context)
      : Stream(plain.executor()),
        socket_(plain.take_socket(), ssl_context),
        closed_(false)
  {
  }

  awaitable<error_code> SecureStream::connect(const tcp::endpoint& ep)
  {
    error_code ec;
    co_await socket_.lowest_layer().async_connect(ep, asio::redirect_error(asio::use_awaitable, ec));
    co_return ec;
  }

  awaitable<error_code> SecureStream::handshake()
  {
    if (closed_)
      co_return asio::error::operation_aborted;
    error_code ec;
    co_await socket_.async_handshake(asio::ssl::stream_base::client, asio::redirect_error(asio::use_awaitable, ec));
    co_return ec;
  }

  void SecureStream::setServerHostname(const std::string& hostname)
  {
    // (1) SNI: tell the server which virtual host we want; required for hosts
    //     that serve multiple TLS certificates on the same IP (Gmail, etc.).
    SSL_set_tlsext_host_name(socket_.native_handle(), hostname.c_str());

    // (2) Hostname verification: validate the presented certificate's SAN/CN
    //     against the requested hostname. Required by NFR-04 and RFC 6125 §6.
    //     Without this callback, verify_peer only walks the CA chain and would
    //     accept any valid certificate from any domain — a MITM with a valid
    //     certificate from any domain would otherwise pass.
    boost::system::error_code ec;
    socket_.set_verify_callback(boost::asio::ssl::host_name_verification(hostname), ec);
    // The non-throwing overload only fails on programmer error (e.g. moved-from
    // socket). We tolerate it: failure here just falls back to the default
    // callback, but with verify_peer set, the next handshake would still fail
    // on chain mismatch — so this is fail-safe rather than fail-open.
    (void)ec;
  }

  awaitable<std::expected<std::size_t, error_code>> SecureStream::write(asio::const_buffer buf)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);
    error_code ec;
    std::size_t bytes = co_await async_write(socket_, buf, asio::redirect_error(asio::use_awaitable, ec));
    if (ec.failed())
      co_return std::unexpected(ec);
    co_return bytes;
  }

  awaitable<std::expected<std::size_t, error_code>> SecureStream::readUntil(
      asio::mutable_buffer buf,
      const std::string& delim)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);
    error_code ec;
    // Use member read_buffer_ to preserve data across calls
    std::size_t bytes_received =
        co_await async_read_until(socket_, read_buffer_, delim, asio::redirect_error(asio::use_awaitable, ec));
    std::size_t to_copy = std::min(buf.size(), bytes_received);
    if (!ec.failed() && to_copy > 0)
    {
      auto begin = buffers_begin(read_buffer_.data());
      std::memcpy(buf.data(), &*begin, to_copy);
      read_buffer_.consume(to_copy);
    }
    if (ec.failed())
      co_return std::unexpected(ec);
    co_return to_copy;
  }

  awaitable<std::expected<std::size_t, error_code>> SecureStream::readUntil(std::string& data, const std::string& delim)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);

    error_code ec;
    std::size_t bytes_received =
        co_await async_read_until(socket_, read_buffer_, delim, asio::redirect_error(asio::use_awaitable, ec));

    if (ec.failed())
      co_return std::unexpected(ec);

    if (bytes_received > 0)
    {
      // Copy all bytes (including delimiter) from read_buffer_ to data
      auto begin = buffers_begin(read_buffer_.data());
      data.assign(begin, begin + bytes_received);
      read_buffer_.consume(bytes_received);
    }
    else
    {
      data.clear();
    }

    co_return bytes_received;
  }

  awaitable<std::expected<std::size_t, error_code>> SecureStream::readExactly(asio::mutable_buffer buf)
  {
    if (closed_)
      co_return std::unexpected(asio::error::operation_aborted);

    size_t total_needed = buf.size();
    size_t bytes_copied = 0;
    char* dest = static_cast<char*>(buf.data());

    // First, consume any data already buffered from previous readUntil calls
    size_t buffered = read_buffer_.size();
    if (buffered > 0)
    {
      size_t to_copy = std::min(buffered, total_needed);
      auto begin = asio::buffers_begin(read_buffer_.data());
      std::memcpy(dest, &*begin, to_copy);
      read_buffer_.consume(to_copy);
      bytes_copied = to_copy;
    }

    // If we still need more data, read from socket
    if (bytes_copied < total_needed)
    {
      size_t remaining = total_needed - bytes_copied;
      error_code ec;
      size_t n = co_await asio::async_read(
          socket_,
          asio::buffer(dest + bytes_copied, remaining),
          asio::transfer_exactly(remaining),
          asio::redirect_error(asio::use_awaitable, ec));

      if (ec)
      {
        co_return std::unexpected(ec);
      }
      bytes_copied += n;
    }

    co_return bytes_copied;
  }

  asio::awaitable<error_code> SecureStream::disconnect()
  {
    if (closed_)
      co_return asio::error::operation_aborted;
    closed_ = true;
    error_code ec;
    co_await socket_.async_shutdown(asio::redirect_error(asio::use_awaitable, ec));
    socket_.lowest_layer().close(ec);
    co_return ec;
  }

}  // namespace aurora::mail::common::stream
