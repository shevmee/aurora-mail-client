#include <StartupConfig.hpp>

namespace aurora::mail::common::config
{

  uint16_t SmtpConfig::getPort() const
  {
    return port.value_or(getSmtpPort(mode));
  }

  uint16_t ImapConfig::getPort() const
  {
    return port.value_or(getImapPort(mode));
  }

}  // namespace aurora::mail::common::config
