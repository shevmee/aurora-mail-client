#include <ReceivedMailMessage.hpp>
#include <algorithm>
#include <cctype>

namespace aurora::mail::common::mail
{

  std::string ReceivedMailMessage::getHeader(const std::string& name) const
  {
    std::string lower_name = name;
    std::transform(
        lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });

    auto it = headers.find(lower_name);
    return (it != headers.end()) ? it->second : std::string();
  }

  bool ReceivedMailMessage::hasHeader(const std::string& name) const
  {
    std::string lower_name = name;
    std::transform(
        lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });
    return headers.count(lower_name) > 0;
  }

}  // namespace aurora::mail::common::mail
