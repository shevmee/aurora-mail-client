#include <ImapTokenizer.hpp>
#include <cctype>
#include <charconv>
#include <format>
#include <system_error>

namespace aurora::mail::imap::parser
{
  char Tokenizer::peek() const
  {
    return pos_ < input_.size() ? input_[pos_] : '\0';
  }
  char Tokenizer::get()
  {
    return pos_ < input_.size() ? input_[pos_++] : '\0';
  }
  bool Tokenizer::isEof() const
  {
    return pos_ >= input_.size();
  }

  void Tokenizer::skipSpaces()
  {
    while (!this->isEof() && (input_[pos_] == ' ' || input_[pos_] == '\t'))
    {
      pos_++;
    }
  }

  void Tokenizer::skipLineEndings()
  {
    while (!this->isEof() && (input_[pos_] == '\r' || input_[pos_] == '\n'))
    {
      pos_++;
    }
  }

  std::expected<Value, std::string> Tokenizer::nextValue()
  {
    skipSpaces();
    if (this->isEof())
    {
      return std::unexpected("End of stream");
    }

    char c = this->peek();

    // IMAP "tag" tokens '*' (untagged) and '+' (continuation request) are
    // standalone single-character atoms. They are listed as delimiters in
    // readAtomLike() because they cannot appear *inside* an atom, but they are
    // perfectly valid as a whole token on their own — and they are in fact the
    // first token of every greeting and every untagged server response.
    //
    // Before this special-case, readAtomLike() returned an empty string for a
    // leading '*' which made the parser reject every IMAP greeting with
    // "Invalid IMAP greeting format".
    if (c == '*' || c == '+')
    {
      std::size_t start = pos_;
      pos_++;
      return Atom{ input_.substr(start, 1) };
    }

    if (c == '"')
    {
      return readQuoted().transform([](std::string&& s) -> Value { return Quoted{ std::move(s) }; });
    }

    if (c == '(')
    {
      // get() already advances pos_; the previous "pos_++" right after it was a
      // double-advance that silently skipped the first byte of every parenthesised
      // list (e.g. the leading '"' or first atom of a FETCH parenthesised list).
      this->get();  // consume '('
      auto lst = std::make_unique<List>();
      skipSpaces();

      while (!this->isEof() && this->peek() != ')')
      {
        auto v = nextValue();
        if (!v)
          return std::unexpected(v.error());
        lst->items.push_back(std::move(*v));
        skipSpaces();
      }

      if (this->get() != ')')
      {
        return std::unexpected(std::format("Unclosed list at position {}", pos_));
      }

      return lst;
    }

    if (c == '{')
    {
      auto literal_result = readLiteralLength();
      if (!literal_result)
        return std::unexpected(literal_result.error());

      size_t n = *literal_result;
      if (pos_ + n > input_.size())
      {
        return std::unexpected(std::format("Incomplete literal data at position {}", pos_));
      }

      std::string_view token = input_.substr(pos_, n);
      pos_ += n;
      return Literal{ token };
    }

    // Number or atom
    std::string_view token = readAtomLike();
    if (token.empty())
    {
      return std::unexpected(std::format("Expected value at position {}", pos_));
    }

    if (token == "NIL" || token == "nil")
    {
      return Nil{};
    }

    long long val;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
    if (ec == std::errc{} && ptr == token.data() + token.size())
    {
      return Number{ val };
    }

    return Atom{ token };
  }

  std::expected<std::string, std::string> Tokenizer::readQuoted()
  {
    this->get();  // consume opening '"'
    std::string out;
    out.reserve(1 << 5);

    while (!this->isEof())
    {
      char c = this->get();
      if (c == '\\')
      {
        if (this->isEof())
          return std::unexpected(std::format("Unterminated escape sequence at position {}", pos_));
        out.push_back(this->get());
        continue;
      }
      if (c == '"')
      {
        return out;
      }
      out.push_back(c);
    }

    return std::unexpected(std::format("Unterminated quoted string at position {}", pos_));
  }

  std::expected<size_t, std::string> Tokenizer::readLiteralLength()
  {
    this->get();  // consume '{'
    std::size_t start_pos = pos_;

    while (!this->isEof() && std::isdigit(static_cast<unsigned char>(this->peek())) != 0)
    {
      pos_++;
    }

    if (this->get() != '}')
    {
      return std::unexpected(std::format("Expected '}}' in literal at position {}", pos_));
    }

    // Handling of \r\n
    if (peek() == '\r')
      get();
    if (peek() == '\n')
      get();

    std::size_t len = 0;
    std::string_view num_str = input_.substr(start_pos, pos_ - start_pos);
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), len);
    if (ec != std::errc{})
    {
      return std::unexpected(std::format("Invalid literal length at {}", start_pos));
    }

    return len;
  }

  std::string_view Tokenizer::readAtomLike()
  {
    std::size_t start = pos_;
    while (!this->isEof())
    {
      char c = this->peek();
      if (c <= ' ' || c == '(' || c == ')' || c == '{' || c == '"' || c == '%' || c == '*' || c == '\\')
      {
        break;
      }
      pos_++;
    }
    return input_.substr(start, pos_ - start);
  }

}  // namespace aurora::mail::imap::parser
