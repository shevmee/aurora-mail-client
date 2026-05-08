#include <Base64.hpp>
#include <boost/beast/core/detail/base64.hpp>

namespace aurora::mail::common::base64
{
  namespace beast = boost::beast::detail;

  std::string base64Encode(std::string_view decoded)
  {
    std::size_t encoded_size = beast::base64::encoded_size(decoded.size());
    std::string encoded_output(encoded_size, '\0');
    beast::base64::encode(encoded_output.data(), decoded.data(), decoded.size());
    return encoded_output;
  }

  std::string base64Decode(std::string_view encoded)
  {
    std::size_t decoded_size = beast::base64::decoded_size(encoded.size());
    std::string decoded_output(decoded_size, '\0');
    beast::base64::decode(decoded_output.data(), encoded.data(), encoded.size());
    return decoded_output;
  }

}  // namespace aurora::mail::common::base64
