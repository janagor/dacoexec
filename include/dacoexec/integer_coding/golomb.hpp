//! \file
//! Golomb coding with truncated binary remainders and a positive divisor.

#ifndef DACOEXEC_INTEGER_CODING_GOLOMB_HPP
#define DACOEXEC_INTEGER_CODING_GOLOMB_HPP

#include <bit>
#include <cstdint>
#include <dacoexec/bit/bit_reader.hpp>
#include <dacoexec/integer_coding/common.hpp>
#include <limits>
#include <stdexcept>

namespace dacoexec::integer_coding {

/** Returns the encoded size; throws overflow_error if it cannot fit uint64_t. */
[[nodiscard]] constexpr auto golomb_bit_length(std::uint64_t value, std::uint32_t divisor) -> std::uint64_t
{
  detail::validate_golomb(divisor);
  auto const width = static_cast<unsigned>(std::bit_width(divisor - 1U));
  auto const cutoff = (std::uint64_t{ 1 } << width) - divisor;
  auto const remainder_bits = value % divisor < cutoff ? width - 1U : width;
  auto const suffix_bits = std::uint64_t{ remainder_bits } + 1U;
  auto const quotient = value / divisor;
  if (quotient > std::numeric_limits<std::uint64_t>::max() - suffix_bits) {
    throw std::overflow_error("dacoexec::integer_coding Golomb size overflow");
  }
  return quotient + suffix_bits;
}

namespace detail {

  // Kept separate so full-domain arithmetic can be tested without multi-gigabit prefixes.
  struct golomb_parts
  {
    std::uint64_t quotient_ = 0;
    std::uint64_t remainder_ = 0;
  };

  constexpr auto golomb_value(golomb_parts parts, std::uint32_t divisor) -> std::uint64_t
  {
    auto const [quotient, remainder] = parts;
    auto const maximum = std::numeric_limits<std::uint64_t>::max();
    if (quotient > maximum / divisor) { throw std::overflow_error("dacoexec::integer_coding Golomb overflow"); }
    auto const base = quotient * divisor;
    if (remainder > maximum - base) { throw std::overflow_error("dacoexec::integer_coding Golomb overflow"); }
    return base + remainder;
  }

}// namespace detail

template<bit_output Writer> constexpr auto write_golomb(Writer &out, std::uint64_t value, std::uint32_t divisor) -> void
{
  detail::validate_golomb(divisor);
  detail::write_unary(out, value / divisor);
  auto const width = static_cast<unsigned>(std::bit_width(divisor - 1U));
  auto const cutoff = (std::uint64_t{ 1 } << width) - divisor;
  auto const remainder = value % divisor;
  if (remainder < cutoff) {
    detail::write_value(out, { .value_ = remainder, .bit_count_ = width - 1U });
  } else {
    detail::write_value(out, { .value_ = remainder + cutoff, .bit_count_ = width });
  }
}

template<bit_input Reader>
constexpr auto read_golomb(Reader &reader, std::uint32_t divisor, decode_limits limits = {}) -> std::uint64_t
{
  detail::validate_golomb(divisor);
  auto const maximum = std::numeric_limits<std::uint64_t>::max();
  auto const quotient = detail::read_unary(reader, limits, maximum / divisor);
  auto const width = static_cast<unsigned>(std::bit_width(divisor - 1U));
  std::uint64_t remainder = 0;
  if (width != 0U) {
    auto const cutoff = (std::uint64_t{ 1 } << width) - divisor;
    remainder = reader.read_bits(width - 1U);
    if (remainder >= cutoff) { remainder = ((remainder << 1U) | reader.read_bits(1U)) - cutoff; }
  }
  return detail::golomb_value({ .quotient_ = quotient, .remainder_ = remainder }, divisor);
}

constexpr auto read_golomb(bit_reader &reader, std::uint32_t divisor, decode_limits limits = {}) -> std::uint64_t
{ return read_golomb<bit_reader>(reader, divisor, limits); }

}// namespace dacoexec::integer_coding

#endif// DACOEXEC_INTEGER_CODING_GOLOMB_HPP
