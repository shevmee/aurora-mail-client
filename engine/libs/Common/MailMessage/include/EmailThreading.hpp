#ifndef EMAIL_THREADING_HPP
#define EMAIL_THREADING_HPP
#include <optional>
#include <string>
#include <vector>

namespace aurora::mail::common::mail
{
  struct EmailThreading
  {
    std::string message_id;
    std::optional<std::string> in_reply_to;
    std::vector<std::string> references;

    bool isReply() const
    {
      return in_reply_to.has_value();
    }
  };
}  // namespace aurora::mail::common::mail

#endif  // EMAIL_THREADING_HPP