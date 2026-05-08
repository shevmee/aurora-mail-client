#ifndef IMAP_VALUE_HPP
#define IMAP_VALUE_HPP

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aurora::mail::imap::parser
{

  /**
   * @brief Represents an IMAP atom (unquoted string token)
   */
  struct Atom
  {
    std::string_view value;
    bool operator==(const Atom&) const = default;
  };

  /**
   * @brief Represents an IMAP quoted string "..."
   */
  struct Quoted
  {
    std::string value;
    bool operator==(const Quoted&) const = default;
  };

  /**
   * @brief Represents an IMAP literal value {n}\r\ndata
   */
  struct Literal
  {
    std::string_view data;
    size_t size() const
    {
      return data.size();
    }
    bool operator==(const Literal&) const = default;
  };

  using Number = long long;
  using Nil = std::monostate;

  struct List;
  using Value = std::variant<Atom, Quoted, Literal, Number, Nil, std::unique_ptr<List>>;

  /**
   * @brief Represents an IMAP list (...)
   */
  struct List
  {
    std::vector<Value> items;
    bool operator==(const List& other) const
    {
      return items == other.items;
    }
  };

  inline std::optional<std::string_view> getAtomValue(const Value& v)
  {
    if (auto* atom = std::get_if<Atom>(&v))
      return atom->value;
    return std::nullopt;
  }

  inline std::optional<std::string_view> getQuotedValue(const Value& v)
  {
    if (auto* quoted = std::get_if<Quoted>(&v))
      return quoted->value;
    return std::nullopt;
  }
}  // namespace aurora::mail::imap::parser

#endif  // IMAP_VALUE_HPP
