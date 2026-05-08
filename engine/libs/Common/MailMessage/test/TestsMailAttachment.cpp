#include <gtest/gtest.h>

#include <filesystem>

#include "MailAttachment.hpp"

TEST(MailAttachmentTest, GetPath)
{
  std::filesystem::path test_path = "/path/to/attachment.txt";
  aurora::mail::common::mail::MailAttachment attachment(test_path);

  EXPECT_EQ(attachment.getPath(), test_path);
}

TEST(MailAttachmentTest, GetName)
{
  std::filesystem::path test_path = "/path/to/attachment.txt";
  aurora::mail::common::mail::MailAttachment attachment(test_path);

  EXPECT_EQ(attachment.getName(), "attachment.txt");
}

TEST(MailAttachmentTest, GetNameNoExtension)
{
  std::filesystem::path test_path = "/path/to/attachment";
  aurora::mail::common::mail::MailAttachment attachment(test_path);

  EXPECT_EQ(attachment.getName(), "attachment");
}

TEST(MailAttachmentTest, GetNameWithSpecialCharacters)
{
  std::filesystem::path test_path = "/path/to/@ttachment!.txt";
  aurora::mail::common::mail::MailAttachment attachment(test_path);

  EXPECT_EQ(attachment.getName(), "@ttachment!.txt");
}
