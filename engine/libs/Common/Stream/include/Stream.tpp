namespace aurora::mail::common::stream {
template <typename AwaitableFunc>
auto TimedOutStream::withTimeout(AwaitableFunc &&func)
    -> awaitable<typename decltype(func())::value_type>
    requires TimeoutRepresentable<typename decltype(func())::value_type>
{
    using namespace asio::experimental::awaitable_operators;
    using ResultType = typename decltype(func())::value_type;
    
    // Create a timer awaitable
    auto timeout_coro = [this]() -> awaitable<void> {
        asio::steady_timer timer(executor(), timeout_);
        co_await timer.async_wait(asio::use_awaitable);
    };
    
    // Race the operation against the timeout
    // Returns std::variant<ResultType, void> - index 0 = op finished, index 1 = timeout
    auto result = co_await (std::forward<AwaitableFunc>(func)() || timeout_coro());
    
    if (result.index() == 0) {
        // Operation completed before timeout
        co_return std::get<0>(result);
    } else {
        // Timeout occurred - return appropriate error
        if constexpr (std::is_same_v<ResultType, error_code>) {
            co_return asio::error::timed_out;
        } else if constexpr (requires { ResultType{std::unexpected(error_code{})}; }) {
            co_return std::unexpected(asio::error::timed_out);
        }
    }
}
} // namespace aurora::mail::common::stream
