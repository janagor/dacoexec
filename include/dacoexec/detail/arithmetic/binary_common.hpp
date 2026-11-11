//! \file
//! Inclusive interval conventions for the E1/E2/E3 binary arithmetic engine.
#ifndef DACOEXEC_ARITHMETIC_BINARY_COMMON_HPP
#define DACOEXEC_ARITHMETIC_BINARY_COMMON_HPP

#include <cstdint>
#include <dacoexec/detail/arithmetic/binary_model.hpp>
#include <limits>

namespace dacoexec::detail::arithmetic {
inline constexpr std::uint32_t binary_initial_high = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t binary_half = 1U << 31U;
inline constexpr std::uint32_t binary_quarter = 1U << 30U;
inline constexpr std::uint32_t binary_three_quarters = binary_half + binary_quarter;
inline constexpr unsigned binary_state_bits = 32;

//! Zero occupies [low, split-1], one occupies [split, high]. The normalized
//! interval is wider than 2^30; totals <= 2^16 keep both subintervals nonempty.
[[nodiscard]] inline auto binary_split(std::uint32_t low, std::uint32_t high, binary_model const &probabilities)
  -> std::uint32_t
{
  auto const total = probabilities.total();
  auto const width = std::uint64_t{ high } - low + 1;
  return static_cast<std::uint32_t>(low + width * probabilities.zero_frequency_ / total);
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_COMMON_HPP
