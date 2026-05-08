#ifndef IMAP_TOKENIZER_HPP
#define IMAP_TOKENIZER_HPP

#include <ImapValue.hpp>
#include <expected>
#include <string_view>

namespace aurora::mail::imap::parser
{

  /**
   * @brief Tokenizes IMAP responses into structured values
   */
  class Tokenizer
  {
   public:
    explicit Tokenizer(std::string_view input) : input_(input), pos_(0)
    {
    }

    std::expected<Value, std::string> nextValue();
    size_t position() const
    {
      return pos_;
    }
    void skipSpaces();
    /// Advance past any consecutive '\r' and '\n' bytes. Used by the response
    /// parser to step from one untagged line to the next; skipSpaces() is NOT
    /// a substitute because it intentionally only skips ' ' and '\t'.
    void skipLineEndings();

   private:
    std::string_view input_;
    size_t pos_;

   private:
    char peek() const;
    char get();
    bool isEof() const;

    std::expected<std::string, std::string> readQuoted();
    std::expected<size_t, std::string> readLiteralLength();
    std::string_view readAtomLike();
  };

}  // namespace aurora::mail::imap::parser

#endif  // IMAP_TOKENIZER_HPP
