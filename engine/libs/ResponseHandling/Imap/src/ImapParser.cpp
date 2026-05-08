#include <ImapParser.hpp>
#include <ImapTokenizer.hpp>
#include <format>

namespace
{

  // Trim whitespace
  [[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept
  {
    const size_t start = s.find_first_not_of("\r\n\t ");
    if (start == std::string_view::npos) {
      return std::string_view{};
    }
    const size_t end = s.find_last_not_of("\r\n\t ");
    return s.substr(start, end - start + 1);
  }
}  // namespace

namespace aurora::mail::imap::response
{

  std::expected<ImapResponse, std::string> parse(std::string_view raw_response, bool is_greeting)
  {
    ImapResponse resp;
    parser::Tokenizer tok(raw_response);

    while (tok.position() < raw_response.size())
    {
      std::size_t line_start = tok.position();

      // Read the first token (tag: '*', '+', or 'A001')
      auto tag_val = tok.nextValue();
      if (!tag_val) break;

      auto tag_str_opt = parser::getAtomValue(*tag_val);
      if (!tag_str_opt) {
        return std::unexpected(std::format("Expected tag at position {}", line_start));
      }
      std::string_view tag = *tag_str_opt;

      // + is continuation request (e.g., for AUTHENTICATE)
      if (tag == "*" || tag == "+")
      {
        UntaggedResponse ur;
        ur.line = raw_response.substr(line_start);

        auto cmd_val = tok.nextValue();
        if (cmd_val) {
          if (auto cmd_str = parser::getAtomValue(*cmd_val)) {
            ur.command = *cmd_str;
          }
          ur.parsed_values.push_back(std::move(*cmd_val));
        }

        size_t data_start = tok.position();

        // Read remaining values until end of line (CRLF)
        // Tokenizer will correctly skip through literals {N}\r\n...
        while (tok.position() < raw_response.size()) {
            // If we reached the end of the line (outside literals)
            if (raw_response[tok.position()] == '\r' || raw_response[tok.position()] == '\n') {
                break;
            }
            
            auto val = tok.nextValue();
            if (!val) break;
            ur.parsed_values.push_back(std::move(*val));
        }

        // Calculate raw data without allocations and std::visit!
        size_t data_end = tok.position();
        ur.data = trim(raw_response.substr(data_start, data_end - data_start));
        
        // Correct the length of the entire string
        ur.line = trim(raw_response.substr(line_start, data_end - line_start));
        
        resp.untagged.push_back(std::move(ur));

        // Step past the CR/LF separating this untagged line from the next one.
        // The previous implementation called tok.skipSpaces() in a loop, but
        // skipSpaces only consumes ' ' and '\t' — so the loop never advanced
        // past '\r' and the parser silently dropped every line after the first.
        tok.skipLineEndings();
      }
      else
      {
        // --- Parse Tagged Response (End of command) ---
        resp.tag = tag;
        
        auto status_val = tok.nextValue();
        if (status_val) {
            if (auto status_str = parser::getAtomValue(*status_val)) {
                resp.status = stringToStatusType(*status_str);
            }
        }

        size_t text_start = tok.position();
        // Find the end of the line
        size_t text_end = raw_response.find('\n', text_start);
        if (text_end == std::string_view::npos) text_end = raw_response.size();
        
        resp.text = trim(raw_response.substr(text_start, text_end - text_start));
        
        // Tagged response is always the last, end parsing
        break;
      }
    }

    // Validation logic for Greeting (almost unchanged, but now works with string_view)
    if (is_greeting)
    {
      if (!resp.untagged.empty())
      {
        const auto& first = resp.untagged[0];
        resp.tag = "*";
        resp.status = stringToStatusType(std::string{first.command}); // stringToStatusType accepts string_view
        resp.text = first.data;

        if (resp.status != StatusType::OK && resp.status != StatusType::PREAUTH && resp.status != StatusType::BYE)
        {
          return std::unexpected(std::format("Invalid IMAP greeting status: '{}'", first.command));
        }
        return resp;
      }
      return std::unexpected("Invalid IMAP greeting format");
    }

    if (resp.tag.empty() && !resp.untagged.empty()) {
        resp.tag = "*";
        resp.status = StatusType::OK;
        resp.text = "Untagged server notification";
    }

    return resp;
  }

  bool isGreetingLine(std::string_view line) noexcept
  {
    return line.starts_with("* OK") || 
           line.starts_with("* PREAUTH") || 
           line.starts_with("* BYE");
  }

}  // namespace aurora::mail::imap::response
