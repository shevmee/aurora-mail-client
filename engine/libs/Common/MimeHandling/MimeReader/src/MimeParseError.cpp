#include <MimeParseError.hpp>

namespace aurora::mail::common::mime
{

  MimeParseError::MimeParseError(Type t, std::string msg) : type(t), message(std::move(msg))
  {
  }

  std::string MimeParseError::toString() const
  {
    std::string type_str;
    switch (type)
    {
      case Type::InvalidFormat: type_str = "InvalidFormat"; break;
      case Type::EncodingError: type_str = "EncodingError"; break;
      case Type::CharsetError: type_str = "CharsetError"; break;
      case Type::PartExtractionError: type_str = "PartExtractionError"; break;
      case Type::EmptyMessage: type_str = "EmptyMessage"; break;
    }
    return type_str + ": " + message;
  }

}  // namespace aurora::mail::common::mime
