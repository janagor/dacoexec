//! \file
//! Independent signed and positive-domain integer mappings.

#ifndef DACOEXEC_NUMERIC_MAPPING_HPP
#define DACOEXEC_NUMERIC_MAPPING_HPP

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace dacoexec::numeric {

/** Maps 0,-1,1,-2,... to 0,1,2,3,..., including INT64_MIN. */
[[nodiscard]] constexpr auto zigzag_encode(std::int64_t value) noexcept -> std::uint64_t
{
  if (value < 0) { return (static_cast<std::uint64_t>(-(value + 1)) << 1U) | 1U; }
  return static_cast<std::uint64_t>(value) << 1U;
}

[[nodiscard]] constexpr auto zigzag_decode(std::uint64_t value) noexcept -> std::int64_t
{
  auto const magnitude = static_cast<std::int64_t>(value >> 1U);
  return (value & 1U) != 0U ? -magnitude - 1 : magnitude;
}

/** Shifts a zero-based value into Gamma/Delta's domain; UINT64_MAX cannot fit. */
[[nodiscard]] constexpr auto to_positive(std::uint64_t value) -> std::uint64_t
{
  if (value == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("dacoexec::numeric positive mapping overflow");
  }
  return value + 1U;
}

[[nodiscard]] constexpr auto from_positive(std::uint64_t value) -> std::uint64_t
{
  if (value == 0U) { throw std::invalid_argument("dacoexec::numeric positive mapping requires a positive value"); }
  return value - 1U;
}

}// namespace dacoexec::numeric

#endif// DACOEXEC_NUMERIC_MAPPING_HPP
