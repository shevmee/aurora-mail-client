#include <MailAttachment.hpp>

namespace aurora::mail::common::mail
{
  MailAttachment::MailAttachment(std::filesystem::path path) : path_(std::move(path))
  {
  }

  const std::filesystem::path& MailAttachment::getPath() const
  {
    return path_;
  }

  std::string MailAttachment::getName() const
  {
    return path_.filename().string();
  }
}  // namespace aurora::mail::common::mail
