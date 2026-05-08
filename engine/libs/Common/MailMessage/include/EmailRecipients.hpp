#ifndef EMAIL_RECEP_HPP
#define EMAIL_RECEP_HPP

#include <MailAddress.hpp>
#include <array>
#include <ranges>
#include <span>
#include <vector>

namespace aurora::mail::common::mail
{
  struct EmailRecipients
  {
    /**
     * @brief List of primary recipients (To field).
     */
    std::vector<MailAddress> to;

    /**
     * @brief List of carbon copy recipients (CC field).
     */
    std::vector<MailAddress> cc;

    /**
     * @brief List of blind carbon copy recipients (BCC field).
     */
    std::vector<MailAddress> bcc;

    /**
     * @brief Returns a view over all recipients (To, CC, BCC).
     *
     * Useful for SMTP envelope which needs all recipients regardless of type.
     */
    auto all() const
    {
      return std::array{ std::span(to), std::span(cc), std::span(bcc) } | std::views::join;
    }
  };
}  // namespace aurora::mail::common::mail

#endif  // EMAIL_RECEP_HPP