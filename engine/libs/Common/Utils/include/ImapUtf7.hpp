#ifndef IMAP_UTF7_HPP
#define IMAP_UTF7_HPP

#include <string>

namespace aurora::mail::common::utils
{

  /**
   * @brief Decode IMAP Modified UTF-7 to UTF-8
   *
   * IMAP uses a modified version of UTF-7 (RFC 3501 Section 5.1.3):
   * - & is the shift character (instead of +)
   * - &- represents the literal & character
   * - Base64 uses , instead of /
   * - No padding (=) is used
   *
   * Examples:
   *   "INBOX" → "INBOX" (ASCII, no change)
   *   "&BB8EPgQ8BDUERwQ1BD0EPQRLBDU-" → "Помеченные" (Russian)
   *   "[Gmail]/&BBIEQQRP- &BD8EPgRHBEIEMA-" → "[Gmail]/Вся почта" (Russian)
   *
   * @param imap_utf7 IMAP Modified UTF-7 encoded string
   * @return UTF-8 decoded string
   */
  std::string decodeImapUtf7(const std::string& imap_utf7);

  /**
   * @brief Encode UTF-8 to IMAP Modified UTF-7
   *
   * @param utf8 UTF-8 string
   * @return IMAP Modified UTF-7 encoded string
   */
  std::string encodeImapUtf7(const std::string& utf8);

}  // namespace aurora::mail::common::utils

#endif  // IMAP_UTF7_HPP
