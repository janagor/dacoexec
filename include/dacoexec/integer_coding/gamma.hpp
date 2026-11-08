//! \file
//! Elias Gamma coding of positive uint64_t values.

#ifndef DACOEXEC_INTEGER_CODING_GAMMA_HPP
#define DACOEXEC_INTEGER_CODING_GAMMA_HPP

#include <bit>
#include <cstdint>
#include <dacoexec/bit/bit_reader.hpp>
#include <dacoexec/integer_coding/common.hpp>
#include <stdexcept>

namespace dacoexec::integer_coding {

template<bit_output Writer> constexpr auto write_gamma(Writer &out, std::uint64_t value) -> void
{
  (void)gamma_bit_length(value);
  auto const width = static_cast<unsigned>(std::bit_width(value));
  detail::write_value(out, { .value_ = 0U, .bit_count_ = width - 1U });
  detail::write_value(out, { .value_ = value, .bit_count_ = width });
}

// Malformed values may consume input before throwing; the reader remains bounded.
template<bit_input Reader> constexpr auto read_gamma(Reader &reader) -> std::uint64_t
{
  unsigned zeros = 0;
  while (!reader.read_bit()) {
    ++zeros;
    if (zeros >= integer_bits) { throw std::overflow_error("dacoexec::integer_coding gamma overflow"); }
  }
  return (std::uint64_t{ 1 } << zeros) | reader.read_bits(zeros);
}

constexpr auto read_gamma(bit_reader &reader) -> std::uint64_t { return read_gamma<bit_reader>(reader); }

}// namespace dacoexec::integer_coding

#endif// DACOEXEC_INTEGER_CODING_GAMMA_HPP
