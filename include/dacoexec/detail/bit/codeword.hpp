//! \file
#ifndef DACOEXEC_DETAIL_BIT_CODEWORD_HPP
#define DACOEXEC_DETAIL_BIT_CODEWORD_HPP
#include <cstdint>

namespace dacoexec::detail::bit {


// Largest codeword accepted by bit_writer. A 32-bit mask is built with a
// 64-bit shift so a 32-bit integer is never shifted by 32.
inline constexpr unsigned max_codeword_bits = 32U;

enum class codeword_status : std::uint8_t {
  ok,
  count_too_large,
  value_too_wide,
};

// Mask of the low `bit_count` bits. `bit_count` must be 0..32.
[[nodiscard]] constexpr auto low_bit_mask(unsigned bit_count) noexcept -> std::uint32_t
{ return static_cast<std::uint32_t>((std::uint64_t{ 1 } << bit_count) - std::uint64_t{ 1 }); }

// `bit_count` must be 0..32 for ok. Wider counts are rejected. A value is
// too wide when any bit above that width is set. Zero width allows only zero.
template<class Bits> [[nodiscard]] constexpr auto check_codeword(Bits bits) noexcept -> codeword_status
{
  auto const [value, bit_count] = bits;
  if (bit_count > max_codeword_bits) { return codeword_status::count_too_large; }
  std::uint32_t const mask = low_bit_mask(bit_count);
  if ((value & ~mask) != 0U) { return codeword_status::value_too_wide; }
  return codeword_status::ok;
}

}// namespace dacoexec::detail::bit
#endif
