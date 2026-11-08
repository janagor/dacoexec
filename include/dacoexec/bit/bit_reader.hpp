//! \file
//! Non-owning, bounded MSB-first bit input.

#ifndef DACOEXEC_BIT_BIT_READER_HPP
#define DACOEXEC_BIT_BIT_READER_HPP

#include <dacoexec/detail/byte.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace dacoexec {

/**
 * Reads only the declared valid bits of a byte span. Storage must outlive the reader.
 * Invalid widths throw invalid_argument; exhausted reads throw out_of_range.
 * Failed read_bits calls leave the position unchanged. Padding is never consumed;
 * validating padding bytes belongs to the containing format.
 */
class bit_reader
{
public:
  constexpr explicit bit_reader(std::span<std::byte const> bytes, std::size_t valid_bits)
    : bytes_(bytes), remaining_(valid_bits)
  {
    auto const whole_bytes = valid_bits / detail::bits_per_byte;
    auto const partial_bits = valid_bits % detail::bits_per_byte;
    if (whole_bytes > bytes.size() || (whole_bytes == bytes.size() && partial_bits != 0U)) {
      throw std::invalid_argument("dacoexec::bit_reader bit limit exceeds storage");
    }
  }

  [[nodiscard]] constexpr auto remaining_bits() const noexcept -> std::size_t { return remaining_; }

  constexpr auto read_bit() -> bool
  {
    if (remaining_ == 0U) { throw std::out_of_range("dacoexec::bit_reader exhausted input"); }
    auto const shift = detail::bits_per_byte - 1U - static_cast<unsigned>(position_ % detail::bits_per_byte);
    auto const value = std::to_integer<unsigned>(bytes_.subspan(position_ / detail::bits_per_byte).front());
    ++position_;
    --remaining_;
    return ((value >> shift) & 1U) != 0U;
  }

  constexpr auto read_bits(unsigned count) -> std::uint64_t
  {
    if (count > std::numeric_limits<std::uint64_t>::digits) {
      throw std::invalid_argument("dacoexec::bit_reader width exceeds uint64_t");
    }
    if (count > remaining_) { throw std::out_of_range("dacoexec::bit_reader exhausted input"); }
    std::uint64_t value = 0;
    for (unsigned index = 0; index < count; ++index) { value = (value << 1U) | (read_bit() ? 1U : 0U); }
    return value;
  }

private:
  std::span<std::byte const> bytes_;
  std::size_t remaining_ = 0;
  std::size_t position_ = 0;
};

}// namespace dacoexec

#endif// DACOEXEC_BIT_BIT_READER_HPP
