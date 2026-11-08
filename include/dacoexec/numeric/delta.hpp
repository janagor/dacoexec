//! \file
//! Elias Delta coding of positive uint64_t values.

#ifndef DACOEXEC_NUMERIC_DELTA_HPP
#define DACOEXEC_NUMERIC_DELTA_HPP

#include <bit>
#include <cstdint>
#include <dacoexec/bit/bit_reader.hpp>
#include <dacoexec/numeric/common.hpp>
#include <dacoexec/numeric/gamma.hpp>
#include <stdexcept>

namespace dacoexec::numeric {

template<bit_output Writer> constexpr auto write_delta(Writer &out, std::uint64_t value) -> void
{
  (void)delta_bit_length(value);
  auto const width = static_cast<unsigned>(std::bit_width(value));
  write_gamma(out, width);
  detail::write_value(out, { .value_ = value, .bit_count_ = width - 1U });
}

constexpr auto read_delta(bit_reader &reader) -> std::uint64_t
{
  auto const width = read_gamma(reader);
  if (width > integer_bits) { throw std::overflow_error("dacoexec::numeric delta overflow"); }
  auto const suffix = static_cast<unsigned>(width - 1U);
  return (std::uint64_t{ 1 } << suffix) | reader.read_bits(suffix);
}

}// namespace dacoexec::numeric

#endif// DACOEXEC_NUMERIC_DELTA_HPP
