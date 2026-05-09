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
    // See engine/libs/Common/Utils/src/Base64.cpp for the bug background:
    // beast::base64::decoded_size() is an upper bound; we must trim to the
    // actual byte count returned by decode() or the result will contain
    // trailing NUL bytes for any input without '=' padding.
    std::size_t decoded_size = beast::base64::decoded_size(encoded.size());
    std::string decoded_output(decoded_size, '\0');
    auto const [written, _] = beast::base64::decode(decoded_output.data(), encoded.data(), encoded.size());
    decoded_output.resize(written);
    return decoded_output;
  }

}  // namespace aurora::mail::common::base64
