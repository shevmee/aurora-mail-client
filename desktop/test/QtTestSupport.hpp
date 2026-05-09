#ifndef AURORA_DESKTOP_QT_TEST_SUPPORT_HPP
#define AURORA_DESKTOP_QT_TEST_SUPPORT_HPP

// Stream operators for Qt value types so that GoogleTest's
// `EXPECT_*( ... ) << qstr` formatting works without forcing every call site
// to spell out `.toStdString()`. Include this header from desktop test files
// that pass QString / QByteArray through GTest's message stream.
//
// We register the operators in `namespace testing::internal` because that is
// where GTest's PrintToStringParamName / DefaultPrintTo lookups happen, and
// also at namespace scope so direct `std::ostream& << QString` works.

#include <QByteArray>
#include <QChar>
#include <QString>
#include <ostream>

inline std::ostream& operator<<(std::ostream& os, const QString& s)
{
  return os << s.toStdString();
}

inline std::ostream& operator<<(std::ostream& os, const QByteArray& b)
{
  // Print as a quoted string when it's printable; otherwise print byte
  // length. Either way avoids dumping huge raw payloads into the test log.
  return os << "QByteArray(size=" << b.size() << ")";
}

inline std::ostream& operator<<(std::ostream& os, const QChar& c)
{
  return os << "QChar(0x" << std::hex << static_cast<int>(c.unicode()) << std::dec << ")";
}

#endif  // AURORA_DESKTOP_QT_TEST_SUPPORT_HPP
