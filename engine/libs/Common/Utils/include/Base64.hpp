#ifndef BASE64_HPP
#define BASE64_HPP

#include <string>
#include <string_view>

namespace aurora::mail::common::base64
{

  /**
   * @brief Encodes a string into Base64 format.
   *
   * This function takes a string as a `std::string_view`, and encodes it into a
   * Base64-encoded string. It uses the Boost Beast library to perform the
   * encoding.
   */
  std::string base64Encode(std::string_view decoded);

  /**
   * @brief Decodes a Base64-encoded string back to its original form.
   *
   * This function takes a Base64-encoded string as input and decodes it back to
   * its original binary or text form using Boost Beast.
   */
  std::string base64Decode(std::string_view encoded);

}  // namespace aurora::mail::common::base64

#endif  // BASE64_HPP
