//! \file
#ifndef DACOEXEC_MODEL_NORMALIZATION_HPP
#define DACOEXEC_MODEL_NORMALIZATION_HPP

#include <cstdint>

namespace dacoexec::detail::model {

struct frequency_share
{
  std::uint32_t quotient_;
  std::uint64_t remainder_;
};

struct proportional_share_parameters
{
  std::uint64_t count_;
  std::uint32_t slots_;
  std::uint64_t sum_;
  unsigned precision_;
};

// Computes count * slots / sum and its remainder without forming the product.
// The caller supplies count <= sum, sum > 0, and slots <= 2^precision.
[[nodiscard]] inline auto proportional_share(proportional_share_parameters parameters) -> frequency_share
{
  auto const [count, slots, sum, precision] = parameters;
  std::uint64_t remainder = 0;
  std::uint32_t quotient = 0;
  for (unsigned bit = precision + 1; bit != 0; --bit) {
    quotient *= 2;
    if (remainder >= sum - remainder) {
      remainder -= sum - remainder;
      ++quotient;
    } else {
      remainder += remainder;
    }
    if (((slots >> (bit - 1)) & 1U) != 0) {
      if (remainder >= sum - count) {
        remainder -= sum - count;
        ++quotient;
      } else {
        remainder += count;
      }
    }
  }
  return { .quotient_ = quotient, .remainder_ = remainder };
}

}// namespace dacoexec::detail::model
#endif// DACOEXEC_MODEL_NORMALIZATION_HPP
