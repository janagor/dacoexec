//! \file
//! Shared integer coding operations and decoder resource limits.

#ifndef DACOEXEC_INTEGER_CODING_COMMON_HPP
#define DACOEXEC_INTEGER_CODING_COMMON_HPP

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/bit/bit_reader.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace dacoexec::integer_coding {

inline constexpr unsigned integer_bits = std::numeric_limits<std::uint64_t>::digits;
inline constexpr std::uint64_t default_max_unary_bits = std::numeric_limits<std::uint64_t>::max();

/** Maximum unary prefix per value, excluding its zero; defaults to unrestricted. */
struct decode_limits
{
  std::uint64_t max_unary_bits_ = default_max_unary_bits;
};

template<class Writer>
concept bit_output = requires(Writer &out, bit_sequence bits, bool bit) {
  { out.write_bits(bits) } -> std::same_as<void>;
  { out.write_bit(bit) } -> std::same_as<void>;
};

[[nodiscard]] constexpr auto gamma_bit_length(std::uint64_t value) -> unsigned
{
  if (value == 0U) { throw std::invalid_argument("dacoexec::integer_coding gamma requires a positive integer"); }
  auto const width = static_cast<unsigned>(std::bit_width(value));
  return width + (width - 1U);
}

[[nodiscard]] constexpr auto delta_bit_length(std::uint64_t value) -> unsigned
{
  if (value == 0U) { throw std::invalid_argument("dacoexec::integer_coding delta requires a positive integer"); }
  auto const width = static_cast<unsigned>(std::bit_width(value));
  return gamma_bit_length(width) + (width - 1U);
}

namespace detail {

  struct integer_bit_sequence
  {
    std::uint64_t value_ = 0;
    unsigned bit_count_ = 0;
  };

  template<bit_output Writer> constexpr auto write_value(Writer &out, integer_bit_sequence bits) -> void
  {
    auto const [value, bit_count] = bits;
    unsigned count = bit_count;
    while (count != 0U) {
      auto const chunk = count > dacoexec::detail::max_codeword_bits ? dacoexec::detail::max_codeword_bits : count;
      auto const shift = count - chunk;
      auto const part = static_cast<std::uint32_t>(value >> shift) & dacoexec::detail::low_bit_mask(chunk);
      out.write_bits({ .value_ = part, .bit_count_ = chunk });
      count -= chunk;
    }
  }

  template<bit_output Writer> constexpr auto write_unary(Writer &out, std::uint64_t quotient) -> void
  {
    while (quotient != 0U) {
      auto const chunk = quotient > dacoexec::detail::max_codeword_bits ? dacoexec::detail::max_codeword_bits
                                                                        : static_cast<unsigned>(quotient);
      out.write_bits({ .value_ = dacoexec::detail::low_bit_mask(chunk), .bit_count_ = chunk });
      quotient -= chunk;
    }
    out.write_bit(false);
  }

  constexpr auto read_unary(bit_reader &reader, decode_limits limits, std::uint64_t maximum) -> std::uint64_t
  {
    std::uint64_t quotient = 0;
    while (reader.read_bit()) {
      if (quotient == maximum) { throw std::overflow_error("dacoexec::integer_coding unary overflow"); }
      if (quotient == limits.max_unary_bits_) {
        throw std::length_error("dacoexec::integer_coding unary limit exceeded");
      }
      ++quotient;
    }
    return quotient;
  }

  constexpr auto validate_rice(unsigned shift) -> void
  {
    if (shift >= integer_bits) { throw std::invalid_argument("dacoexec::integer_coding Rice shift exceeds domain"); }
  }

  constexpr auto validate_golomb(std::uint32_t divisor) -> void
  {
    if (divisor == 0U) { throw std::invalid_argument("dacoexec::integer_coding Golomb divisor must be positive"); }
  }

}// namespace detail
}// namespace dacoexec::integer_coding

#endif// DACOEXEC_INTEGER_CODING_COMMON_HPP
