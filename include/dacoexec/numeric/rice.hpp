//! \file
//! Rice coding of nonnegative integers with q ones followed by zero.

#ifndef DACOEXEC_NUMERIC_RICE_HPP
#define DACOEXEC_NUMERIC_RICE_HPP

#include <cstdint>
#include <dacoexec/bit/bit_reader.hpp>
#include <dacoexec/numeric/common.hpp>
#include <limits>
#include <stdexcept>

namespace dacoexec::numeric {

/** Returns the encoded size; throws overflow_error if it cannot fit uint64_t. */
[[nodiscard]] constexpr auto rice_bit_length(std::uint64_t value, unsigned shift) -> std::uint64_t
{
  detail::validate_rice(shift);
  auto const quotient = value >> shift;
  auto const suffix_bits = std::uint64_t{ shift } + 1U;
  if (quotient > std::numeric_limits<std::uint64_t>::max() - suffix_bits) {
    throw std::overflow_error("dacoexec::numeric Rice size overflow");
  }
  return quotient + suffix_bits;
}

// The shift must be below integer_bits. Small shifts can produce very large outputs.
// Size estimation is optional: even a size above uint64_t is a valid codeword.
template<bit_output Writer> constexpr auto write_rice(Writer &out, std::uint64_t value, unsigned shift) -> void
{
  detail::validate_rice(shift);
  detail::write_unary(out, value >> shift);
  detail::write_value(out, { .value_ = value, .bit_count_ = shift });
}

constexpr auto read_rice(bit_reader &reader, unsigned shift, decode_limits limits = {}) -> std::uint64_t
{
  detail::validate_rice(shift);
  auto const quotient = detail::read_unary(reader, limits, std::numeric_limits<std::uint64_t>::max() >> shift);
  return (quotient << shift) | reader.read_bits(shift);
}

}// namespace dacoexec::numeric

#endif// DACOEXEC_NUMERIC_RICE_HPP
