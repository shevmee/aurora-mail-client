#ifndef PROTOCOL_CONCEPTS_HPP
#define PROTOCOL_CONCEPTS_HPP

#include <concepts>
#include <string>
#include <variant>

#include "ProtocolError.hpp"

namespace aurora::mail::common
{

  /**
   * @brief Concept for protocol command types that produce wire bytes.
   *
   * Commands return Result<std::string> from serialize() so that input
   * validation failures (CRLF injection guard, missing tokens, etc.) propagate
   * through the same std::expected pipeline used by the rest of the stack
   * instead of throwing or returning malformed wire data.
   *
   * Trivial fixed commands (Quit, Data, ...) simply wrap their literal in a
   * successful Result; the cost on the success path is a single tag byte.
   */
  template<typename T>
  concept Serializable = requires(const T& obj) {
    { obj.serialize() } -> std::same_as<Result<std::string>>;
  };

  /**
   * @brief Concept for protocol commands with serialization and naming.
   *
   * Commands must provide:
   * - serialize() method returning Result<std::string>
   * - static name() method for logging/debugging
   */
  template<typename Cmd>
  concept ProtocolCommand = Serializable<Cmd> && requires {
    { Cmd::name() } -> std::convertible_to<std::string_view>;
  };

  /**
   * @brief Helper to validate command variant types at compile-time.
   */
  template<typename... Ts>
  struct CommandChecker
  {
    static_assert((ProtocolCommand<Ts> && ...), "One of the command types is not a ProtocolCommand!");
    using type = std::variant<Ts...>;
  };

}  // namespace aurora::mail::common

#endif  // PROTOCOL_CONCEPTS_HPP
