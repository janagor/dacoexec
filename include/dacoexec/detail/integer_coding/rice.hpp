//! \file
//! Rice coding of nonnegative integers with q ones followed by zero.

#ifndef DACOEXEC_INTEGER_CODING_RICE_HPP
#define DACOEXEC_INTEGER_CODING_RICE_HPP

#include <cstdint>
#include <dacoexec/bit.hpp>
#include <dacoexec/detail/integer_coding/common.hpp>
#include <dacoexec/error.hpp>
#include <limits>
#include <stdexcept>

namespace dacoexec::detail::integer_coding {

/** Returns the encoded size; throws dacoexec::exception (errc::numeric_overflow) if it cannot fit uint64_t. */
[[nodiscard]] constexpr auto rice_bit_length(std::uint64_t value, unsigned shift) -> std::uint64_t
{
  ::dacoexec::detail::integer_coding::validate_rice(shift);
  auto const quotient = value >> shift;
  auto const suffix_bits = std::uint64_t{ shift } + 1U;
  if (quotient > std::numeric_limits<std::uint64_t>::max() - suffix_bits) {
    throw ::dacoexec::exception(
      ::dacoexec::errc::numeric_overflow, "dacoexec::detail::integer_coding Rice size overflow");
  }
  return quotient + suffix_bits;
}

// The shift must be below integer_bits. Small shifts can produce very large outputs.
// Size estimation is optional: even a size above uint64_t is a valid codeword.
template<bit_output Writer> constexpr auto write_rice(Writer &out, std::uint64_t value, unsigned shift) -> void
{
  ::dacoexec::detail::integer_coding::validate_rice(shift);
  ::dacoexec::detail::integer_coding::write_unary(out, value >> shift);
  ::dacoexec::detail::integer_coding::write_value(out, { .value_ = value, .bit_count_ = shift });
}

template<bit_input Reader>
constexpr auto read_rice(Reader &reader, unsigned shift, decode_limits limits = {}) -> std::uint64_t
{
  ::dacoexec::detail::integer_coding::validate_rice(shift);
  auto const quotient =
    ::dacoexec::detail::integer_coding::read_unary(reader, limits, std::numeric_limits<std::uint64_t>::max() >> shift);
  return (quotient << shift) | reader.read_bits(shift);
}

constexpr auto read_rice(bit_reader &reader, unsigned shift, decode_limits limits = {}) -> std::uint64_t
{ return read_rice<bit_reader>(reader, shift, limits); }

}// namespace dacoexec::detail::integer_coding

#endif// DACOEXEC_INTEGER_CODING_RICE_HPP
