//! \file
//! Byte/bit and integer range adaptors. Concrete view and iterator types are unspecified.
#ifndef DACOEXEC_VIEWS_HPP
#define DACOEXEC_VIEWS_HPP
#include <cstdint>
#include <dacoexec/detail/views/bits.hpp>
#include <dacoexec/detail/views/integer_decode.hpp>
#include <dacoexec/detail/views/integer_encode.hpp>
#include <limits>

namespace dacoexec::views {
//! Lazily yields MSB-first bool bits from byte input.
[[nodiscard]] constexpr auto bits() { return ::dacoexec::detail::views::bits(); }

//! Maximum unary prefix per decoded integer, excluding its terminating zero.
struct decode_limits
{
  std::uint64_t max_unary_bits_ = std::numeric_limits<std::uint64_t>::max();
};

namespace encode {
  [[nodiscard]] constexpr auto gamma() { return ::dacoexec::detail::views::encode::gamma(); }

  [[nodiscard]] constexpr auto delta() { return ::dacoexec::detail::views::encode::delta(); }

  [[nodiscard]] constexpr auto golomb(std::uint32_t divisor)
  { return ::dacoexec::detail::views::encode::golomb(divisor); }

  [[nodiscard]] constexpr auto rice(unsigned shift) { return ::dacoexec::detail::views::encode::rice(shift); }
}// namespace encode

namespace decode {
  [[nodiscard]] constexpr auto gamma(std::uint64_t count) { return ::dacoexec::detail::views::decode::gamma(count); }

  [[nodiscard]] constexpr auto delta(std::uint64_t count) { return ::dacoexec::detail::views::decode::delta(count); }

  [[nodiscard]] constexpr auto golomb(std::uint32_t divisor, std::uint64_t count, decode_limits limits = {})
  { return ::dacoexec::detail::views::decode::golomb(divisor, count, { .max_unary_bits_ = limits.max_unary_bits_ }); }

  [[nodiscard]] constexpr auto rice(unsigned shift, std::uint64_t count, decode_limits limits = {})
  { return ::dacoexec::detail::views::decode::rice(shift, count, { .max_unary_bits_ = limits.max_unary_bits_ }); }
}// namespace decode

}// namespace dacoexec::views

#endif// DACOEXEC_VIEWS_HPP
