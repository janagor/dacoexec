//! \file
//! Elias Delta coding of positive uint64_t values.

#ifndef DACOEXEC_INTEGER_CODING_DELTA_HPP
#define DACOEXEC_INTEGER_CODING_DELTA_HPP

#include <bit>
#include <cstdint>
#include <dacoexec/bit.hpp>
#include <dacoexec/detail/integer_coding/common.hpp>
#include <dacoexec/detail/integer_coding/gamma.hpp>
#include <dacoexec/error.hpp>
#include <stdexcept>

namespace dacoexec::detail::integer_coding {

template<bit_output Writer> constexpr auto write_delta(Writer &out, std::uint64_t value) -> void
{
  (void)delta_bit_length(value);
  auto const width = static_cast<unsigned>(std::bit_width(value));
  write_gamma(out, width);
  ::dacoexec::detail::integer_coding::write_value(out, { .value_ = value, .bit_count_ = width - 1U });
}

template<bit_input Reader> constexpr auto read_delta(Reader &reader) -> std::uint64_t
{
  auto const width = read_gamma(reader);
  if (width > integer_bits) {
    throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "dacoexec::detail::integer_coding delta overflow");
  }
  auto const suffix = static_cast<unsigned>(width - 1U);
  return (std::uint64_t{ 1 } << suffix) | reader.read_bits(suffix);
}

constexpr auto read_delta(bit_reader &reader) -> std::uint64_t { return read_delta<bit_reader>(reader); }

}// namespace dacoexec::detail::integer_coding

#endif// DACOEXEC_INTEGER_CODING_DELTA_HPP
