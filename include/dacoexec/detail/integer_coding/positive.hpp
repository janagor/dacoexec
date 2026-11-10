//! \file
//! Checked zero-based to positive-domain integer mapping.
#ifndef DACOEXEC_INTEGER_CODING_POSITIVE_HPP
#define DACOEXEC_INTEGER_CODING_POSITIVE_HPP

#include <cstdint>
#include <dacoexec/error.hpp>
#include <limits>

namespace dacoexec::detail::integer_coding {

/** Shifts a zero-based value into Gamma/Delta's domain; UINT64_MAX cannot fit. */
[[nodiscard]] constexpr auto to_positive(std::uint64_t value) -> std::uint64_t
{
  if (value == std::numeric_limits<std::uint64_t>::max()) {
    throw ::dacoexec::exception(
      ::dacoexec::errc::numeric_overflow, "dacoexec::detail::integer_coding positive mapping overflow");
  }
  return value + 1U;
}

[[nodiscard]] constexpr auto from_positive(std::uint64_t value) -> std::uint64_t
{
  if (value == 0U) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument,
      "dacoexec::detail::integer_coding positive mapping requires a positive value");
  }
  return value - 1U;
}

}// namespace dacoexec::detail::integer_coding
#endif// DACOEXEC_INTEGER_CODING_POSITIVE_HPP
