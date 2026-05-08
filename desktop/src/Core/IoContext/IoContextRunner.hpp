#ifndef IOCONTEXTRUNNER_HPP
#define IOCONTEXTRUNNER_HPP

#include <boost/asio.hpp>
#include <memory>
#include <thread>

namespace asio = boost::asio;

/**
 * @class IoContextRunner
 * @brief Manages a Boost.Asio io_context lifecycle in a dedicated thread.
 *
 * This class is responsible for:
 * - Running a `boost::asio::io_context` in its own background thread
 * - Keeping the io_context alive using a work guard
 * - Cleanly stopping the io_context and joining the thread on destruction
 *
 * @note This class does NOT manage SSL contexts. SSL context should be created
 *       and configured separately where needed, following the Single Responsibility
 *       Principle. This allows:
 *       - Using io_context for non-SSL operations (DNS, plain TCP)
 *       - Multiple SSL contexts with different configurations
 *       - Flexible SSL configuration (TLS versions, verify modes, custom CAs)
 *
 * Example usage:
 * @code
 * IoContextRunner runner;
 * boost::asio::io_context& io = runner.get();
 *
 * // Create SSL context separately where needed.
 * // Use ssl::context::tls_client and narrow with SSL_CTX_set_min/max_proto_version
 * // (TLS 1.3 preferred, fallback to TLS 1.2). See main.cpp::configureSslContext.
 * boost::asio::ssl::context ssl_ctx(ssl::context::tls_client);
 * ssl_ctx.set_verify_mode(ssl::verify_peer);
 * @endcode
 */
class IoContextRunner
{
 public:
  /**
   * @brief Constructs and starts the io_context in a background thread.
   * @param concurrency_hint Hint for optimal thread concurrency (default: 1).
   */
  explicit IoContextRunner(int concurrency_hint = 1);

  /**
   * @brief Stops the io_context and joins the background thread.
   */
  ~IoContextRunner();

  // Non-copyable, non-movable (owns running thread)
  IoContextRunner(const IoContextRunner&) = delete;
  IoContextRunner& operator=(const IoContextRunner&) = delete;
  IoContextRunner(IoContextRunner&&) = delete;
  IoContextRunner& operator=(IoContextRunner&&) = delete;

  /**
   * @brief Gets the managed io_context.
   * @return Reference to the io_context.
   */
  [[nodiscard]] asio::io_context& get() noexcept;
  [[nodiscard]] const asio::io_context& get() const noexcept;

  /**
   * @brief Conversion operator for convenient access.
   */
  [[nodiscard]] operator asio::io_context&() noexcept
  {
    return get();
  }

  /**
   * @brief Checks if the io_context is running.
   * @return True if the background thread is active.
   */
  [[nodiscard]] bool is_running() const noexcept;

 private:
  std::unique_ptr<asio::io_context> io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::jthread thread_;
};

#endif  // IOCONTEXTRUNNER_HPP
