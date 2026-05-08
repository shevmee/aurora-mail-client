#include <gtest/gtest.h>

#include "MailAddress.hpp"
#include "MailAttachment.hpp"
#include "MailMessage.hpp"
#include "MailMessageBuilder.hpp"

TEST(MailMessageBuilderTest, SetFromAndTo)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.from(sender).to(recipient);

  auto message = builder.build();

  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().from.getAddress(), "sender@example.com");
  EXPECT_EQ(message.value().from.getName(), "Sender Name");
  EXPECT_EQ(message.value().email_recipients.to.size(), 1);
  EXPECT_EQ(message.value().email_recipients.to.at(0).getAddress(), "recipient@example.com");
  EXPECT_EQ(message.value().email_recipients.to.at(0).getName(), "Recipient Name");
}

TEST(MailMessageBuilderTest, AddCc)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  aurora::mail::common::mail::MailAddress cc("cc@example.com", "Cc Name");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.from(sender).cc(cc).to(recipient);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().email_recipients.cc.size(), 1);
  EXPECT_EQ(message.value().email_recipients.cc.at(0).getAddress(), "cc@example.com");
  EXPECT_EQ(message.value().email_recipients.cc.at(0).getName(), "Cc Name");
}

TEST(MailMessageBuilderTest, AddBcc)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  aurora::mail::common::mail::MailAddress bcc("bcc@example.com", "Bcc Name");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.from(sender).bcc(bcc).to(recipient);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().email_recipients.bcc.size(), 1);
  EXPECT_EQ(message.value().email_recipients.bcc.at(0).getAddress(), "bcc@example.com");
  EXPECT_EQ(message.value().email_recipients.bcc.at(0).getName(), "Bcc Name");
}

TEST(MailMessageBuilderTest, SetSubject)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.from(sender).subject("Test Subject").to(recipient);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().subject, "Test Subject");
}

TEST(MailMessageBuilderTest, SetBody)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.from(sender).body("Test Body").to(recipient);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().text_body, "Test Body");
}

TEST(MailMessageBuilderTest, AddAttachment)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.from(sender).addAttachment(aurora::mail::common::mail::MailAttachment("/path/to/attachment.txt")).to(recipient);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().attachments_.size(), 1);
  EXPECT_EQ(message.value().attachments_.at(0).getPath(), "/path/to/attachment.txt");
}

TEST(MailMessageBuilderTest, BuildReturnsErrorIfFromNotSet)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient Name");
  builder.to(recipient);

  auto message = builder.build();
  ASSERT_FALSE(message.has_value());
  EXPECT_EQ(message.error(), "From address is not set");
}

TEST(MailMessageBuilderTest, BuildReturnsErrorIfToNotSet)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender Name");
  builder.from(sender);

  auto message = builder.build();
  ASSERT_FALSE(message.has_value());
  EXPECT_EQ(message.error(), "At least one recipient is required");
}

// === ADDITIONAL TESTS ===

TEST(MailMessageBuilderTest, MultipleRecipients)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient1("recipient1@example.com", "Recipient 1");
  aurora::mail::common::mail::MailAddress recipient2("recipient2@example.com", "Recipient 2");

  builder.from(sender).to(recipient1).to(recipient2);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().email_recipients.to.size(), 2);
  EXPECT_EQ(message.value().email_recipients.to.at(0).getAddress(), "recipient1@example.com");
  EXPECT_EQ(message.value().email_recipients.to.at(1).getAddress(), "recipient2@example.com");
}

TEST(MailMessageBuilderTest, MultipleCc)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");
  aurora::mail::common::mail::MailAddress cc1("cc1@example.com", "CC 1");
  aurora::mail::common::mail::MailAddress cc2("cc2@example.com", "CC 2");

  builder.from(sender).to(recipient).cc(cc1).cc(cc2);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().email_recipients.cc.size(), 2);
}

TEST(MailMessageBuilderTest, MultipleBcc)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");
  aurora::mail::common::mail::MailAddress bcc1("bcc1@example.com", "BCC 1");
  aurora::mail::common::mail::MailAddress bcc2("bcc2@example.com", "BCC 2");

  builder.from(sender).to(recipient).bcc(bcc1).bcc(bcc2);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().email_recipients.bcc.size(), 2);
}

TEST(MailMessageBuilderTest, MultipleAttachments)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  builder.from(sender)
      .to(recipient)
      .addAttachment(aurora::mail::common::mail::MailAttachment("/path/to/file1.txt"))
      .addAttachment(aurora::mail::common::mail::MailAttachment("/path/to/file2.txt"));

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().attachments_.size(), 2);
}

TEST(MailMessageBuilderTest, EmptySubject)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  builder.from(sender).to(recipient).subject("");

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_TRUE(message.value().subject.empty());
}

TEST(MailMessageBuilderTest, EmptyBody)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  builder.from(sender).to(recipient).body("");

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_TRUE(message.value().text_body.empty());
}

TEST(MailMessageBuilderTest, CompleteMessage)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");
  aurora::mail::common::mail::MailAddress cc("cc@example.com", "CC");
  aurora::mail::common::mail::MailAddress bcc("bcc@example.com", "BCC");

  builder.from(sender)
      .to(recipient)
      .cc(cc)
      .bcc(bcc)
      .subject("Complete Test Email")
      .body("This is a complete test email with all fields.")
      .addAttachment(aurora::mail::common::mail::MailAttachment("/path/to/attachment.txt"));

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().from.getAddress(), "sender@example.com");
  EXPECT_EQ(message.value().email_recipients.to.size(), 1);
  EXPECT_EQ(message.value().email_recipients.cc.size(), 1);
  EXPECT_EQ(message.value().email_recipients.bcc.size(), 1);
  EXPECT_EQ(message.value().subject, "Complete Test Email");
  EXPECT_FALSE(message.value().text_body.empty());
  EXPECT_EQ(message.value().attachments_.size(), 1);
}

TEST(MailMessageBuilderTest, LongSubject)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  std::string long_subject(500, 'A');
  builder.from(sender).to(recipient).subject(long_subject);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().subject.length(), 500);
}

TEST(MailMessageBuilderTest, LongBody)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  std::string long_body(10000, 'B');
  builder.from(sender).to(recipient).body(long_body);

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().text_body.length(), 10000);
}

TEST(MailMessageBuilderTest, SpecialCharactersInSubject)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  builder.from(sender).to(recipient).subject("Special: !@#$%^&*()");

  auto message = builder.build();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value().subject, "Special: !@#$%^&*()");
}

TEST(MailMessageBuilderTest, BuilderReset_MultipleBuilds)
{
  aurora::mail::common::mail::MailMessageBuilder builder;
  aurora::mail::common::mail::MailAddress sender("sender@example.com", "Sender");
  aurora::mail::common::mail::MailAddress recipient("recipient@example.com", "Recipient");

  builder.from(sender).to(recipient).subject("First");
  auto message1 = builder.build();

  ASSERT_TRUE(message1.has_value());
  EXPECT_EQ(message1.value().subject, "First");
}
