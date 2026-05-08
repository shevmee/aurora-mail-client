#include <Parser.hpp>
#include <algorithm>
#include <cctype>
#include <format>
#include <string_view>

namespace
{

  // Trim trailing whitespace and CRLF
  inline std::string_view rtrim(std::string_view s) noexcept
  {
    const size_t end = s.find_last_not_of("\r\n\t ");
    return (end == std::string_view::npos) ? std::string_view{} : s.substr(0, end + 1);
  }

  inline bool isDigit(char c) noexcept
  {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
  }

  inline int parseStatusCode(std::string_view line) noexcept
  {
    return (line[0] - '0') * 100 +
           (line[1] - '0') * 10 +
           (line[2] - '0');
  }

}  // namespace

namespace aurora::mail::smtp::response
{

  std::expected<SmtpResponse, std::string> parse(const std::string& raw_response)
  {
    SmtpResponse resp;

    // TODO: most likely unused
    resp.raw_response = raw_response;

    std::string_view input(raw_response);
    bool first_line = true;
    std::size_t line_num = 0;

    while (!input.empty())
    {
      line_num++;

      const std::size_t nl = input.find('\n');
      std::string_view line =
        (nl == std::string_view::npos) ? input : input.substr(0, nl);

      input = (nl == std::string_view::npos)
        ? std::string_view{}
        : input.substr(nl + 1);

      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }

      // Skip empty lines
      if (line.empty())
      {
        continue;
      }

      // Line must be at least "XXX " (4 chars)
      if (line.size() < 4)
      {
        return std::unexpected(std::format("SMTP response line {} too short: \"{}\"", line_num, line));
      }

      // First 3 chars must be digits
      if (!std::all_of(line.begin(), line.begin() + 3, isDigit))
      {
        return std::unexpected(std::format("SMTP response line {} missing status code: \"{}\"", line_num, line));
      }

      // char at position 3 is separator ('-' for continuation, ' ' for last line)
      const char separator = line[3];
      if (separator != ' ' && separator != '-')
      {
          return std::unexpected(std::format(
              "SMTP response line {} has invalid separator after status code: \"{}\"",
              line_num, line));
      }

      // Parse status code
      const int code = parseStatusCode(line.substr(0, 3));

      if (first_line)
      {
        resp.code = static_cast<uint16_t>(code);
      }

      // Position 4+ is the text
      std::string_view text = (line.size() > 4)
      ? line.substr(4)
      : std::string_view{};

      // On first line, check for enhanced status code
      if (first_line && !text.empty())
      {
        // Enhanced codes look like "2.1.5 " at the start
        const std::size_t space_pos = text.find(' ');
        const std::string_view first_token =
          (space_pos == std::string_view::npos)
          ? text : text.substr(0, space_pos);

        if (auto enhanced = EnhancedCode::parse(first_token); enhanced.has_value())
        {
          resp.enhanced_code = enhanced;
          text = (space_pos == std::string_view::npos)
            ? std::string_view{}
            : text.substr(space_pos + 1);
        }
      }

      text = rtrim(text);

      if (!text.empty())
      {
        if (!resp.text.empty())
        {
          resp.text.push_back('\n');
        }
        resp.text.append(text.begin(), text.end());
      }

      first_line = false;
    }

    if (resp.code == 0)
    {
      return std::unexpected(std::format(
          "No valid SMTP status code found in response:\n{}",
          raw_response));
    }

    return resp;
  }

}  // namespace aurora::mail::smtp::response
