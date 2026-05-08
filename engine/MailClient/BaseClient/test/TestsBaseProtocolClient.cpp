#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <type_traits>

#include "BaseProtocolClient.hpp"

using namespace aurora::mail::common;
namespace asio = boost::asio;

// === TEST FIXTURE ===

class BaseProtocolClientTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    // Setup io_context and SSL context for tests
  }

  void TearDown() override
  {
    // Cleanup
  }

  asio::io_context io_context;
  asio::ssl::context ssl_context{ asio::ssl::context::tlsv12_client };
};

// === CONSTRUCTION TESTS ===

TEST_F(BaseProtocolClientTest, Construction_SMTP)
{
  EXPECT_NO_THROW({ BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP); });
}

TEST_F(BaseProtocolClientTest, Construction_IMAP)
{
  EXPECT_NO_THROW({ BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::IMAP); });
}

TEST_F(BaseProtocolClientTest, Construction_DifferentTimeouts)
{
  EXPECT_NO_THROW({
    BaseProtocolClient client1(io_context, ssl_context, 10, MailProtocol::SMTP);
    BaseProtocolClient client2(io_context, ssl_context, 30, MailProtocol::SMTP);
    BaseProtocolClient client3(io_context, ssl_context, 60, MailProtocol::IMAP);
  });
}

TEST_F(BaseProtocolClientTest, Construction_ZeroTimeout)
{
  EXPECT_NO_THROW({ BaseProtocolClient client(io_context, ssl_context, 0, MailProtocol::SMTP); });
}

TEST_F(BaseProtocolClientTest, Construction_LargeTimeout)
{
  EXPECT_NO_THROW({ BaseProtocolClient client(io_context, ssl_context, 3600, MailProtocol::IMAP); });
}

// === INITIAL STATE TESTS ===

TEST_F(BaseProtocolClientTest, InitialState_NotConnected)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_FALSE(client.isConnected());
}

TEST_F(BaseProtocolClientTest, InitialState_ServerHostnameEmpty)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_TRUE(client.serverHostname().empty());
}

// === ACCESSOR TESTS ===

TEST_F(BaseProtocolClientTest, GetIoContext_ReturnsValidContext)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_EQ(&client.ioContext(), &io_context);
}

TEST_F(BaseProtocolClientTest, GetSslContext_ReturnsValidContext)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_EQ(&client.sslContext(), &ssl_context);
}

TEST_F(BaseProtocolClientTest, GetProtocolName_SMTP)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_EQ(client.protocolName(), "SMTP");
}

TEST_F(BaseProtocolClientTest, GetProtocolName_IMAP)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::IMAP);

  EXPECT_EQ(client.protocolName(), "IMAP");
}

TEST_F(BaseProtocolClientTest, GetStream_NotNull)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_NE(client.stream(), nullptr);
}

// === MAIL PROTOCOL TESTS ===

TEST_F(BaseProtocolClientTest, MailProtocol_SMTP_ToString)
{
  // After collapsing MailProtocol to a plain enum class, toString() is a free
  // function rather than a member; tests adjusted accordingly.
  EXPECT_EQ(toString(MailProtocol::SMTP), "SMTP");
}

TEST_F(BaseProtocolClientTest, MailProtocol_IMAP_ToString)
{
  EXPECT_EQ(toString(MailProtocol::IMAP), "IMAP");
}

TEST_F(BaseProtocolClientTest, MailProtocol_TypeComparison)
{
  MailProtocol smtp = MailProtocol::SMTP;
  MailProtocol imap = MailProtocol::IMAP;

  EXPECT_NE(smtp, imap);
}

// === CONNECTION MODE TESTS ===

TEST_F(BaseProtocolClientTest, ConnectionMode_EnumValues)
{
  // Just verify enum values exist and are different
  auto plain = ConnectionMode::PLAIN;
  auto starttls = ConnectionMode::STARTTLS;
  auto ssl_tls = ConnectionMode::SSL_TLS;

  EXPECT_NE(static_cast<uint8_t>(plain), static_cast<uint8_t>(starttls));
  EXPECT_NE(static_cast<uint8_t>(starttls), static_cast<uint8_t>(ssl_tls));
}

// === MOVE SEMANTICS TESTS ===

TEST_F(BaseProtocolClientTest, MoveConstruction_Allowed)
{
  // Move construction should be deleted
  EXPECT_FALSE(std::is_move_constructible_v<BaseProtocolClient>);
}

TEST_F(BaseProtocolClientTest, CopyConstruction_NotAllowed)
{
  // Copy constructor should be deleted
  EXPECT_FALSE(std::is_copy_constructible_v<BaseProtocolClient>);
}

TEST_F(BaseProtocolClientTest, CopyAssignment_NotAllowed)
{
  // Copy assignment should be deleted
  EXPECT_FALSE(std::is_copy_assignable_v<BaseProtocolClient>);
}

TEST_F(BaseProtocolClientTest, MoveAssignment_Default)
{
  // Move assignment should be default
  EXPECT_TRUE(std::is_move_assignable_v<BaseProtocolClient>);
}

// === MULTIPLE CLIENTS TESTS ===

TEST_F(BaseProtocolClientTest, MultipleClients_SameContext)
{
  // Multiple clients can share the same io_context and ssl_context
  BaseProtocolClient smtp_client(io_context, ssl_context, 30, MailProtocol::SMTP);
  BaseProtocolClient imap_client(io_context, ssl_context, 30, MailProtocol::IMAP);

  EXPECT_EQ(&smtp_client.ioContext(), &imap_client.ioContext());
  EXPECT_EQ(&smtp_client.sslContext(), &imap_client.sslContext());
}

TEST_F(BaseProtocolClientTest, MultipleClients_DifferentProtocols)
{
  BaseProtocolClient smtp_client(io_context, ssl_context, 30, MailProtocol::SMTP);
  BaseProtocolClient imap_client(io_context, ssl_context, 30, MailProtocol::IMAP);

  EXPECT_EQ(smtp_client.protocolName(), "SMTP");
  EXPECT_EQ(imap_client.protocolName(), "IMAP");
}

TEST_F(BaseProtocolClientTest, MultipleClients_IndependentState)
{
  BaseProtocolClient client1(io_context, ssl_context, 30, MailProtocol::SMTP);
  BaseProtocolClient client2(io_context, ssl_context, 30, MailProtocol::SMTP);

  // Each client should have independent connection state
  EXPECT_FALSE(client1.isConnected());
  EXPECT_FALSE(client2.isConnected());
}

// === VIRTUAL DESTRUCTOR TEST ===

TEST_F(BaseProtocolClientTest, VirtualDestructor_Exists)
{
  // Virtual destructor should exist (for inheritance)
  EXPECT_TRUE(std::is_polymorphic_v<BaseProtocolClient>);
}

// === INHERITANCE TESTS ===

class DerivedProtocolClient : public BaseProtocolClient
{
 public:
  using BaseProtocolClient::BaseProtocolClient;

  // Test that protected members are accessible
  void testProtectedAccess()
  {
    // These should compile if protected access is correct
    auto& ctx = ioContext();
    auto& ssl = sslContext();
    auto* s = stream();
    auto name = protocolName();
    auto hostname = serverHostname();

    (void)ctx;
    (void)ssl;
    (void)s;
    (void)name;
    (void)hostname;
  }
};

TEST_F(BaseProtocolClientTest, Inheritance_ProtectedMembersAccessible)
{
  DerivedProtocolClient derived(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_NO_THROW({ derived.testProtectedAccess(); });
}

TEST_F(BaseProtocolClientTest, Inheritance_PublicInterfaceAvailable)
{
  DerivedProtocolClient derived(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_FALSE(derived.isConnected());
  EXPECT_EQ(derived.protocolName(), "SMTP");
}

// === RESOURCE MANAGEMENT TESTS ===

TEST_F(BaseProtocolClientTest, Destruction_WhileNotConnected)
{
  EXPECT_NO_THROW({
    BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);
    // Destructor should handle cleanup properly
  });
}

TEST_F(BaseProtocolClientTest, ScopedLifetime_MultipleClients)
{
  EXPECT_NO_THROW({
    {
      BaseProtocolClient client1(io_context, ssl_context, 30, MailProtocol::SMTP);
}  // client1 destroyed here

{
  BaseProtocolClient client2(io_context, ssl_context, 30, MailProtocol::IMAP);
}  // client2 destroyed here
});
}

// === SERVER HOSTNAME TESTS ===

TEST_F(BaseProtocolClientTest, ServerHostname_InitiallyEmpty)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  EXPECT_TRUE(client.serverHostname().empty());
}

// === SSL CONTEXT VARIATIONS ===

TEST_F(BaseProtocolClientTest, DifferentSslContexts_Allowed)
{
  asio::ssl::context ctx1{ asio::ssl::context::tlsv12_client };
  asio::ssl::context ctx2{ asio::ssl::context::tlsv13_client };

  EXPECT_NO_THROW({
    BaseProtocolClient client1(io_context, ctx1, 30, MailProtocol::SMTP);
    BaseProtocolClient client2(io_context, ctx2, 30, MailProtocol::IMAP);
  });
}

// === STREAM ACCESS TESTS ===

TEST_F(BaseProtocolClientTest, GetStream_ConsistentPointer)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  auto* stream1 = client.stream();
  auto* stream2 = client.stream();

  EXPECT_EQ(stream1, stream2);
  EXPECT_NE(stream1, nullptr);
}

// === TYPE SAFETY TESTS ===

TEST_F(BaseProtocolClientTest, MailProtocolType_TypeSafety)
{
  // Verify that MailProtocol is now a plain enum class.
  EXPECT_TRUE(std::is_enum_v<MailProtocol>);
}

TEST_F(BaseProtocolClientTest, ConnectionMode_TypeSafety)
{
  // Verify that ConnectionMode is an enum class
  EXPECT_TRUE(std::is_enum_v<ConnectionMode>);
}

// === EDGE CASES ===

TEST_F(BaseProtocolClientTest, NegativeTimeout_HandledCorrectly)
{
  // Negative timeout should be handled (though might be converted to 0 or
  // rejected)
  EXPECT_NO_THROW({ BaseProtocolClient client(io_context, ssl_context, -1, MailProtocol::SMTP); });
}

TEST_F(BaseProtocolClientTest, VeryLargeTimeout_HandledCorrectly)
{
  EXPECT_NO_THROW({ BaseProtocolClient client(io_context, ssl_context, INT_MAX, MailProtocol::SMTP); });
}

// === PROTOCOL TYPE TESTS ===

TEST_F(BaseProtocolClientTest, ProtocolType_UnderlyingType)
{
  // Verify that MailProtocol still has uint8_t as underlying type after the
  // collapse from struct + nested Type enum to a plain enum class.
  EXPECT_TRUE((std::is_same_v<std::underlying_type_t<MailProtocol>, uint8_t>));
}

TEST_F(BaseProtocolClientTest, ConnectionMode_UnderlyingType)
{
  // Verify that ConnectionMode has uint8_t as underlying type
  EXPECT_TRUE((std::is_same_v<std::underlying_type_t<ConnectionMode>, uint8_t>));
}

// === INITIALIZATION ORDER TESTS ===

TEST_F(BaseProtocolClientTest, Construction_ProperInitialization)
{
  BaseProtocolClient client(io_context, ssl_context, 30, MailProtocol::SMTP);

  // All members should be properly initialized
  EXPECT_FALSE(client.isConnected());
  EXPECT_NE(client.stream(), nullptr);
  EXPECT_EQ(&client.ioContext(), &io_context);
  EXPECT_EQ(&client.sslContext(), &ssl_context);
  EXPECT_TRUE(client.serverHostname().empty());
}

// === REFERENCE TESTS ===

TEST_F(BaseProtocolClientTest, IoContext_ReferenceNotCopied)
{
  BaseProtocolClient client1(io_context, ssl_context, 30, MailProtocol::SMTP);
  BaseProtocolClient client2(io_context, ssl_context, 30, MailProtocol::IMAP);

  // Both clients should reference the same io_context
  EXPECT_EQ(&client1.ioContext(), &client2.ioContext());
  EXPECT_EQ(&client1.ioContext(), &io_context);
}

TEST_F(BaseProtocolClientTest, SslContext_ReferenceNotCopied)
{
  BaseProtocolClient client1(io_context, ssl_context, 30, MailProtocol::SMTP);
  BaseProtocolClient client2(io_context, ssl_context, 30, MailProtocol::IMAP);

  // Both clients should reference the same ssl_context
  EXPECT_EQ(&client1.sslContext(), &client2.sslContext());
  EXPECT_EQ(&client1.sslContext(), &ssl_context);
}
