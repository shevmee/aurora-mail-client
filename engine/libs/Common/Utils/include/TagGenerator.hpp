#ifndef TAG_GENERATOR_HPP
#define TAG_GENERATOR_HPP

#include <cstdint>
#include <format>
#include <string>

namespace aurora::mail::common
{

  /**
   * @brief Helper to generate sequential IMAP tags.
   */
  class TagGenerator
  {
   public:
    explicit TagGenerator(std::string prefix = "A") : prefix_(std::move(prefix))
    {
    }

    [[nodiscard]] std::string next()
    {
      return std::format("{}{:03}", prefix_, counter_++);
    }

    void reset(std::uint32_t start = 1) noexcept
    {
      counter_ = start;
    }

   private:
    std::string prefix_;
    std::uint32_t counter_{ 1 };
  };
}  // namespace aurora::mail::common

#endif  // TAG_GENERATOR_HPP
