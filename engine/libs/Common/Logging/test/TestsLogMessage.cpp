#include <gtest/gtest.h>

#include <LogMessage.hpp>
#include <source_location>
#include <string>

using namespace aurora::mail::common::logger;

TEST(LogMessageTest, StoresLevelLineTextAndSourceFields)
{
  const auto ts = std::chrono::system_clock::time_point{};
  LogMessage msg(LogLevel::Info, "hello", ts, "path/to/file.cpp", 42, "myFunc");

  EXPECT_EQ(msg.level, LogLevel::Info);
  EXPECT_EQ(msg.line, 42);
  EXPECT_STREQ(msg.text.data(), "hello");
  EXPECT_STREQ(msg.file.data(), "path/to/file.cpp");
  EXPECT_STREQ(msg.func.data(), "myFunc");
  EXPECT_EQ(msg.ts, ts);
}

TEST(LogMessageTest, TruncatesOversizedText)
{
  const auto ts = std::chrono::system_clock::time_point{};
  std::string huge(MAX_LOG_TEXT_SIZE + 200, 'x');

  LogMessage msg(LogLevel::Warn, huge, ts, std::string_view{}, int{}, std::string_view{});

  EXPECT_EQ(msg.level, LogLevel::Warn);
  EXPECT_EQ(std::strlen(msg.text.data()), MAX_LOG_TEXT_SIZE - 1);

  for (std::size_t i = 0; i < MAX_LOG_TEXT_SIZE - 1; ++i)
  {
    EXPECT_EQ(msg.text[i], 'x');
  }

  EXPECT_EQ(msg.text[MAX_LOG_TEXT_SIZE - 1], '\0');
  EXPECT_EQ(msg.file.front(), '\0');
  EXPECT_EQ(msg.func.front(), '\0');
}

TEST(LogMessageTest, StoresEmptySourceFieldsWhenNotProvided)
{
  const auto ts = std::chrono::system_clock::time_point{};
  LogMessage msg(LogLevel::Debug, "hello", ts, {}, int{}, {});

  EXPECT_EQ(msg.level, LogLevel::Debug);
  EXPECT_EQ(msg.line, 0);
  EXPECT_STREQ(msg.text.data(), "hello");
  EXPECT_EQ(msg.file[0], '\0');
  EXPECT_EQ(msg.func[0], '\0');
}

TEST(LogMessageTest, TruncatesOversizedFile)
{
  const auto ts = std::chrono::system_clock::time_point{};
  std::string huge(MAX_LOG_FILE_SIZE + 100, 'f');

  LogMessage msg(LogLevel::Info, "hello", ts, huge, 123, "func");

  EXPECT_EQ(std::strlen(msg.file.data()), MAX_LOG_FILE_SIZE - 1);

  for (std::size_t i = 0; i < MAX_LOG_FILE_SIZE - 1; ++i)
  {
    EXPECT_EQ(msg.file[i], 'f');
  }

  EXPECT_EQ(msg.file[MAX_LOG_FILE_SIZE - 1], '\0');
}

TEST(LogMessageTest, TruncatesOversizedFunctionName)
{
  const auto ts = std::chrono::system_clock::time_point{};
  std::string huge(MAX_LOG_FUNC_SIZE + 100, 'g');

  LogMessage msg(LogLevel::Info, "hello", ts, "file.cpp", 123, huge);

  EXPECT_EQ(std::strlen(msg.func.data()), MAX_LOG_FUNC_SIZE - 1);

  for (std::size_t i = 0; i < MAX_LOG_FUNC_SIZE - 1; ++i)
  {
    EXPECT_EQ(msg.func[i], 'g');
  }

  EXPECT_EQ(msg.func[MAX_LOG_FUNC_SIZE - 1], '\0');
}

TEST(LogMessageTest, StoresTextExactlyFittingBuffer)
{
  const auto ts = std::chrono::system_clock::time_point{};
  std::string exact(MAX_LOG_TEXT_SIZE - 1, 'a');

  LogMessage msg(LogLevel::Info, exact, ts, {}, 0, {});

  EXPECT_EQ(std::strlen(msg.text.data()), MAX_LOG_TEXT_SIZE - 1);
  EXPECT_STREQ(msg.text.data(), exact.c_str());
  EXPECT_EQ(msg.text[MAX_LOG_TEXT_SIZE - 1], '\0');
}

TEST(LogMessageTest, StoresEmptyText)
{
  const auto ts = std::chrono::system_clock::time_point{};
  LogMessage msg(LogLevel::Info, {}, ts, {}, 0, {});

  EXPECT_EQ(msg.level, LogLevel::Info);
  EXPECT_STREQ(msg.text.data(), "");
  EXPECT_EQ(msg.text[0], '\0');
}

TEST(LogMessageTest, TruncatesAtEmbeddedNullWhenObservedAsCString)
{
  const auto ts = std::chrono::system_clock::time_point{};
  const std::string_view text{ "abc\0def", 7 };

  LogMessage msg(LogLevel::Info, text, ts, {}, 0, {});

  EXPECT_EQ(msg.text[0], 'a');
  EXPECT_EQ(msg.text[1], 'b');
  EXPECT_EQ(msg.text[2], 'c');
  EXPECT_EQ(msg.text[3], '\0');

  // As C-string it stops here
  EXPECT_STREQ(msg.text.data(), "abc");
}

TEST(LogMessageTest, DefaultConstructedMessageIsInValidEmptyState)
{
  LogMessage msg{};

  EXPECT_EQ(msg.line, 0);
  EXPECT_EQ(msg.text[0], '\0');
  EXPECT_EQ(msg.file[0], '\0');
  EXPECT_EQ(msg.func[0], '\0');
}

TEST(LogMessageTest, StreamOutputContainsLevelTextAndSourceLocation)
{
  const auto ts = std::chrono::system_clock::from_time_t(0);
  LogMessage msg(LogLevel::Error, "boom", ts, "main.cpp", 10, "run");

  std::ostringstream oss;
  oss << msg;

  const auto out = oss.str();

  EXPECT_NE(out.find("[ERROR]"), std::string::npos);
  EXPECT_NE(out.find("boom"), std::string::npos);
  EXPECT_NE(out.find("[main.cpp:10 run]"), std::string::npos);
}

TEST(LogMessageTest, ConstructsFromStdSourceLocation)
{
  const auto ts = std::chrono::system_clock::time_point{};
  const auto loc = std::source_location::current();

  LogMessage msg(LogLevel::Info, "hello", ts, loc);

  EXPECT_EQ(msg.level, LogLevel::Info);
  EXPECT_EQ(msg.line, static_cast<int>(loc.line()));
  EXPECT_STREQ(msg.text.data(), "hello");
  EXPECT_GT(std::strlen(msg.file.data()), 0u);
  EXPECT_GT(std::strlen(msg.func.data()), 0u);
  EXPECT_EQ(msg.ts, ts);
}

TEST(LogMessageTest, StreamOutputOmitsSourceLocationWhenSourceFieldsAreEmpty)
{
  const auto ts = std::chrono::system_clock::from_time_t(0);
  LogMessage msg(LogLevel::Info, "hello", ts, {}, 0, {});

  std::ostringstream oss;
  oss << msg;

  const auto out = oss.str();

  EXPECT_NE(out.find("hello"), std::string::npos);
  EXPECT_EQ(out.find("[:]"), std::string::npos);
}
