#include <gtest/gtest.h>

#include <MimeParseError.hpp>

using aurora::mail::common::mime::MimeParseError;

TEST(MimeParseError, ConstructorStoresTypeAndMessage)
{
  MimeParseError err(MimeParseError::Type::EmptyMessage, "no body");
  EXPECT_EQ(err.type, MimeParseError::Type::EmptyMessage);
  EXPECT_EQ(err.message, "no body");
}

TEST(MimeParseError, ToStringIncludesTypeNameAndMessage)
{
  MimeParseError err(MimeParseError::Type::InvalidFormat, "bad headers");
  const std::string str = err.toString();
  EXPECT_NE(str.find("InvalidFormat"), std::string::npos) << str;
  EXPECT_NE(str.find("bad headers"), std::string::npos) << str;
}

TEST(MimeParseError, ToStringForEachKnownType)
{
  struct Case
  {
    MimeParseError::Type type;
    std::string_view label;
  };
  for (const auto& c : { Case{ MimeParseError::Type::InvalidFormat, "InvalidFormat" },
                         Case{ MimeParseError::Type::EncodingError, "EncodingError" },
                         Case{ MimeParseError::Type::CharsetError, "CharsetError" },
                         Case{ MimeParseError::Type::PartExtractionError, "PartExtractionError" },
                         Case{ MimeParseError::Type::EmptyMessage, "EmptyMessage" } })
  {
    MimeParseError err(c.type, "x");
    const std::string str = err.toString();
    EXPECT_NE(str.find(c.label), std::string::npos) << "label '" << c.label << "' not found in toString output: " << str;
  }
}

TEST(MimeParseError, EmptyMessageStillFormats)
{
  MimeParseError err(MimeParseError::Type::EmptyMessage, "");
  const std::string str = err.toString();
  // Format is "<Type>: <message>" -- with empty msg we still expect the
  // "<Type>: " prefix.
  EXPECT_EQ(str, "EmptyMessage: ");
}
