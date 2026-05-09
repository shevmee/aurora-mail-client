#include <gtest/gtest.h>

#include <StartupConfig.hpp>

using aurora::mail::common::config::ConnectionMode;
using aurora::mail::common::config::getImapPort;
using aurora::mail::common::config::getSmtpPort;
using aurora::mail::common::config::ImapConfig;
using aurora::mail::common::config::SmtpConfig;
using aurora::mail::common::config::to_string;

// ---------------------------------------------------------------------------
// Default port helpers
// ---------------------------------------------------------------------------

TEST(StartupConfig, SmtpDefaultPorts)
{
  // RFC 6409 / 8314: 25 (PLAIN), 587 (STARTTLS), 465 (TLS/SMTPS).
  EXPECT_EQ(getSmtpPort(ConnectionMode::PLAIN), 25);
  EXPECT_EQ(getSmtpPort(ConnectionMode::STARTTLS), 587);
  EXPECT_EQ(getSmtpPort(ConnectionMode::SSL_TLS), 465);
}

TEST(StartupConfig, ImapDefaultPorts)
{
  // RFC 3501 / 8314: 143 (PLAIN/STARTTLS), 993 (TLS/IMAPS).
  EXPECT_EQ(getImapPort(ConnectionMode::PLAIN), 143);
  EXPECT_EQ(getImapPort(ConnectionMode::STARTTLS), 143);
  EXPECT_EQ(getImapPort(ConnectionMode::SSL_TLS), 993);
}

TEST(StartupConfig, SmtpUnknownModeFallsBackToStarttls)
{
  // Defensive default: any out-of-range ConnectionMode value should fall
  // through to the STARTTLS port (587). Pin so a future enum extension
  // doesn't silently change the fallback.
  EXPECT_EQ(getSmtpPort(static_cast<ConnectionMode>(99)), 587);
}

TEST(StartupConfig, ImapUnknownModeFallsBackToImaps)
{
  EXPECT_EQ(getImapPort(static_cast<ConnectionMode>(99)), 993);
}

// ---------------------------------------------------------------------------
// SmtpConfig::getPort() / ImapConfig::getPort()
// ---------------------------------------------------------------------------

TEST(SmtpConfigGetPort, FallsBackToDefaultWhenUnset)
{
  SmtpConfig cfg{};
  cfg.host = "smtp.example.com";
  cfg.mode = ConnectionMode::STARTTLS;
  EXPECT_EQ(cfg.getPort(), 587);

  cfg.mode = ConnectionMode::SSL_TLS;
  EXPECT_EQ(cfg.getPort(), 465);

  cfg.mode = ConnectionMode::PLAIN;
  EXPECT_EQ(cfg.getPort(), 25);
}

TEST(SmtpConfigGetPort, OverrideWinsOverDefault)
{
  SmtpConfig cfg{};
  cfg.mode = ConnectionMode::STARTTLS;
  cfg.port = 2525;
  EXPECT_EQ(cfg.getPort(), 2525);
}

TEST(SmtpConfigGetPort, ZeroPortIsHonoured)
{
  // INCONSISTENCY DOC: an explicitly-set port=0 silences the default lookup.
  // 0 is not a valid TCP port, but std::optional has no "valid value" notion
  // so getPort() returns 0 verbatim. Callers must validate the port value
  // separately (e.g. when loading config). Pin so a future "treat 0 as
  // unset" tweak in getPort() is detected.
  SmtpConfig cfg{};
  cfg.mode = ConnectionMode::STARTTLS;
  cfg.port = 0;
  EXPECT_EQ(cfg.getPort(), 0);
}

TEST(ImapConfigGetPort, FallsBackToDefaultWhenUnset)
{
  ImapConfig cfg{};
  cfg.host = "imap.example.com";
  cfg.mode = ConnectionMode::PLAIN;
  EXPECT_EQ(cfg.getPort(), 143);

  cfg.mode = ConnectionMode::STARTTLS;
  EXPECT_EQ(cfg.getPort(), 143);

  cfg.mode = ConnectionMode::SSL_TLS;
  EXPECT_EQ(cfg.getPort(), 993);
}

TEST(ImapConfigGetPort, OverrideWinsOverDefault)
{
  ImapConfig cfg{};
  cfg.mode = ConnectionMode::SSL_TLS;
  cfg.port = 1993;
  EXPECT_EQ(cfg.getPort(), 1993);
}

// ---------------------------------------------------------------------------
// to_string(ConnectionMode)
// ---------------------------------------------------------------------------

TEST(ConnectionModeToString, KnownValues)
{
  EXPECT_EQ(to_string(ConnectionMode::PLAIN), "PLAIN");
  EXPECT_EQ(to_string(ConnectionMode::STARTTLS), "STARTTLS");
  EXPECT_EQ(to_string(ConnectionMode::SSL_TLS), "TLS");
}

TEST(ConnectionModeToString, OutOfRangeReturnsUnknown)
{
  // Hardened path for out-of-range enum values (e.g. UB-by-cast in a config
  // loader). The function should not return junk; it returns "UNKNOWN".
  EXPECT_EQ(to_string(static_cast<ConnectionMode>(255)), "UNKNOWN");
}
