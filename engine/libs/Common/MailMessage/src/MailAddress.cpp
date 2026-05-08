#include <MailAddress.hpp>

namespace aurora::mail::common::mail
{
  std::expected<MailAddress, std::string> MailAddress::create(std::string email, std::string name)
  {
    if (!isValidEmail(email))
    {
      return std::unexpected("Invalid email format: " + email);
    }
    return MailAddress(std::move(email), std::move(name));
  }

  MailAddress::MailAddress(std::string email, std::string name) : address_(std::move(email)), name_(std::move(name))
  {
  }

  const std::string& MailAddress::getAddress() const
  {
    return address_;
  }

  const std::string& MailAddress::getName() const
  {
    return name_;
  }

  bool MailAddress::operator==(const MailAddress& other) const
  {
    return address_ == other.address_;
  }

  bool MailAddress::operator!=(const MailAddress& other) const
  {
    return !(*this == other);
  }

  bool MailAddress::isValid() const
  {
    return !address_.empty() && isValidEmail(address_);
  }

}  // namespace aurora::mail::common::mail
