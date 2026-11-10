//! \file
//! Shared constants and argument checks for the byte-oriented range engine.
#ifndef DACOEXEC_ARITHMETIC_RANGE_COMMON_HPP
#define DACOEXEC_ARITHMETIC_RANGE_COMMON_HPP

#include <cstddef>
#include <cstdint>
#include <dacoexec/error.hpp>
#include <limits>

namespace dacoexec::detail::arithmetic {
inline constexpr unsigned range_byte_bits = 8;
inline constexpr std::uint32_t range_byte_mask = 0xFFU;
inline constexpr std::uint32_t range_initial = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t range_normalization = 1U << 24U;
inline constexpr std::uint32_t range_max_total = range_normalization;
inline constexpr std::size_t range_flush_bytes = 5;

inline void validate_range_total(std::uint32_t total)
{
  if (total == 0 || total > range_max_total) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Range total must be in [1, 2^24]");
  }
}

inline void validate_range_interval(std::uint32_t cumulative, std::uint32_t frequency, std::uint32_t total)
{
  validate_range_total(total);
  if (frequency == 0 || cumulative >= total || frequency > total - cumulative) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid range symbol interval");
  }
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_RANGE_COMMON_HPP
