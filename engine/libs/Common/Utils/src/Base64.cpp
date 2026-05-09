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
    // beast::base64::decoded_size() is an *upper bound* — the real number of
    // decoded bytes is only known after the call. The previous implementation
    // ignored that return value, which left trailing NUL bytes in the output
    // (visible whenever the input had no '=' padding). Several call sites then
    // compensated with `substr(0, original.length())`, which masked the bug
    // for callers who happened to know the original length, while breaking
    // any caller that needed an exact result (e.g. IMAP Modified UTF-7
    // decoding, which feeds the bytes straight into a UTF-16BE → UTF-8
    // converter and emits literal U+0000 codepoints in folder names).
    std::size_t decoded_size = beast::base64::decoded_size(encoded.size());
    std::string decoded_output(decoded_size, '\0');
    auto const [written, _] = beast::base64::decode(decoded_output.data(), encoded.data(), encoded.size());
    decoded_output.resize(written);
    return decoded_output;
  }

}  // namespace aurora::mail::common::base64
