//! \file
//! Signed-to-unsigned ZigZag integer mapping.
#ifndef DACOEXEC_INTEGER_CODING_ZIGZAG_HPP
#define DACOEXEC_INTEGER_CODING_ZIGZAG_HPP

#include <cstdint>

namespace dacoexec::detail::integer_coding {

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

}// namespace dacoexec::detail::integer_coding
#endif// DACOEXEC_INTEGER_CODING_ZIGZAG_HPP
