#ifndef STREAM_HPP
#define STREAM_HPP

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <chrono>
#include <expected>
#include <memory>
#include <string>

namespace aurora::mail::common::stream
{

  namespace asio = boost::asio;
  using asio::awaitable;
  using asio::ip::tcp;
  using boost::system::error_code;

  template<typename T>
  concept TimeoutRepresentable = requires {
    { T{} } -> std::same_as<T>;
  } && (std::is_same_v<T, error_code> || requires { T{ std::unexpected(error_code{}) }; });

  struct StreamExtractionError
  {
    enum class Type : std::uint8_t
    {
      NoInnerStream,
      TypeMismatch
    } type;

    std::string expected_type;
    std::string actual_type;

    std::string message() const
    {
      switch (type)
      {
        case Type::NoInnerStream: return "No inner stream to extract";
        case Type::TypeMismatch: return std::format("Type mismatch: expected '{}', got '{}'", expected_type, actual_type);
        default: return "Unknown error";
      }
    }
  };

  class Stream
  {
   public:
    using executor_type = asio::any_io_executor;

    explicit Stream(executor_type exec);
    virtual ~Stream() = default;

    virtual awaitable<error_code> connect(const tcp::endpoint& ep) = 0;
    virtual awaitable<error_code> disconnect() = 0;
    virtual awaitable<std::expected<std::size_t, error_code>> write(asio::const_buffer buf) = 0;
    virtual awaitable<std::expected<std::size_t, error_code>> readUntil(
        asio::mutable_buffer buf,
        const std::string& delim) = 0;
    virtual awaitable<std::expected<std::size_t, error_code>> readUntil(std::string& data, const std::string& delim) = 0;
    virtual awaitable<std::expected<std::size_t, error_code>> readExactly(asio::mutable_buffer buf) = 0;

    executor_type executor() const noexcept;

   protected:
    executor_type exec_;
  };

  // Decorator
  class TimedOutStream : public Stream
  {
   public:
    // TODO: limit timeout to sth
    TimedOutStream(std::unique_ptr<Stream> stream, std::chrono::milliseconds timeout);

    awaitable<error_code> connect(const tcp::endpoint& ep) override;
    awaitable<error_code> disconnect() override;
    awaitable<std::expected<std::size_t, error_code>> write(asio::const_buffer buf) override;
    awaitable<std::expected<std::size_t, error_code>> readUntil(asio::mutable_buffer buf, const std::string& delim) override;
    awaitable<std::expected<std::size_t, error_code>> readUntil(std::string& data, const std::string& delim) override;
    awaitable<std::expected<std::size_t, error_code>> readExactly(asio::mutable_buffer buf) override;

    /**
     * @brief Extracts the inner stream as a specific type (e.g., PlainStream).
     *
     * This is useful for stream upgrades (e.g., STARTTLS) where you need to
     * extract the plain stream to wrap it in a secure stream.
     *
     * @tparam T The expected stream type (must inherit from Stream)
     * @return expected containing unique_ptr on success, or StreamExtractionError
     * on failure
     */
    /** Per-operation read/write timeout. IDLE and similar long-poll reads may temporarily raise this. */
    std::chrono::milliseconds timeout() const noexcept
    {
      return timeout_;
    }
    void set_timeout(std::chrono::milliseconds m) noexcept
    {
      timeout_ = m;
    }

    template<typename T>
      requires std::is_base_of_v<Stream, T>
    std::expected<std::unique_ptr<T>, StreamExtractionError> extract_inner_as()
    {
      auto inner = take_inner_stream();
      if (!inner)
      {
        return std::unexpected(
            StreamExtractionError{
                .type = StreamExtractionError::Type::NoInnerStream, .expected_type = {}, .actual_type = {} });
      }

      Stream* raw_ptr = inner.get();
      auto* typed_ptr = dynamic_cast<T*>(raw_ptr);
      if (!typed_ptr)
      {
        return std::unexpected(
            StreamExtractionError{ .type = StreamExtractionError::Type::TypeMismatch,
                                   .expected_type = typeid(T).name(),
                                   .actual_type = typeid(*raw_ptr).name() });
      }

      [[maybe_unused]] auto* released = inner.release();
      return std::unique_ptr<T>(typed_ptr);
    }

   private:
    std::unique_ptr<Stream> stream_;
    std::chrono::milliseconds timeout_;

    template<typename AwaitableFunc>
    auto withTimeout(AwaitableFunc&& func) -> awaitable<typename decltype(func())::value_type>
      requires TimeoutRepresentable<typename decltype(func())::value_type>;

    std::unique_ptr<Stream> take_inner_stream();
  };

  class PlainStream : public Stream
  {
   private:
    tcp::socket socket_;
    asio::streambuf read_buffer_;
    bool closed_;

   public:
    explicit PlainStream(const asio::any_io_executor& exec);
    PlainStream(const PlainStream&) = delete;
    PlainStream& operator=(const PlainStream&) = delete;

    awaitable<error_code> connect(const tcp::endpoint& ep) override;

    // NOTE: provide here only strings wrapped with \r\n for proper SMTP/IMAP
    // protocols
    awaitable<std::expected<std::size_t, error_code>> write(asio::const_buffer buf) override;

    // TODO: rewrite with awaitable<system::result<string>> when migrate to Boost
    // > 1.84
    awaitable<std::expected<std::size_t, error_code>> readUntil(asio::mutable_buffer buf, const std::string& delim) override;

    awaitable<std::expected<std::size_t, error_code>> readUntil(std::string& data, const std::string& delim) override;

    awaitable<std::expected<std::size_t, error_code>> readExactly(asio::mutable_buffer buf) override;

    asio::awaitable<error_code> disconnect() override;
    tcp::socket take_socket() noexcept;
  };

  class SecureStream : public Stream
  {
   public:
    SecureStream(const asio::any_io_executor& exec, asio::ssl::context& ssl_context);
    SecureStream(PlainStream&& plain, asio::ssl::context& ssl_context);
    SecureStream(const SecureStream&) = delete;
    SecureStream& operator=(const SecureStream&) = delete;

    awaitable<error_code> connect(const tcp::endpoint& ep) override;
    awaitable<error_code> handshake();

    /**
     * @brief Set the server hostname for SNI (Server Name Indication).
     * Must be called before handshake() for proper certificate verification.
     */
    void setServerHostname(const std::string& hostname);

    awaitable<std::expected<std::size_t, error_code>> write(asio::const_buffer buf) override;

    awaitable<std::expected<std::size_t, error_code>> readUntil(asio::mutable_buffer buf, const std::string& delim) override;

    awaitable<std::expected<std::size_t, error_code>> readUntil(std::string& data, const std::string& delim) override;

    awaitable<std::expected<std::size_t, error_code>> readExactly(asio::mutable_buffer buf) override;

    asio::awaitable<error_code> disconnect() override;

   private:
    asio::ssl::stream<tcp::socket> socket_;
    bool closed_;
    asio::streambuf read_buffer_{};
  };

}  // namespace aurora::mail::common::stream

#include "Stream.tpp"

#endif  // STREAM_HPP
