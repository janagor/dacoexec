//! \file
//! Integer binary arithmetic decoder; the caller supplies models and bit count.
#ifndef DACOEXEC_ARITHMETIC_BINARY_DECODER_HPP
#define DACOEXEC_ARITHMETIC_BINARY_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/binary/common.hpp>
#include <span>

namespace dacoexec::detail::arithmetic {
class binary_arithmetic_decoder
{
public:
  //! Borrow the payload and initialize the code from exactly 32 MSB-first bits.
  explicit binary_arithmetic_decoder(std::span<std::byte const> payload) : payload_(payload)
  {
    for (unsigned index = 0; index < binary_state_bits; ++index) { code_ = (code_ << 1U) | read_bit(); }
  }

  [[nodiscard]] auto decode(binary_model const &probabilities) -> bool
  {
    require_active();
    auto const split = binary_split(low_, high_, probabilities);
    if (code_ < low_ || code_ > high_) { invalid_payload(); }
    auto const bit = code_ >= split;
    if (bit) {
      low_ = split;
    } else {
      high_ = split - 1;
    }
    for (;;) {
      if (high_ < binary_half) {
        // E1: no offset to subtract.
      } else if (low_ >= binary_half) {
        low_ -= binary_half;
        high_ -= binary_half;
        code_ -= binary_half;
      } else if (low_ >= binary_quarter && high_ < binary_three_quarters) {
        low_ -= binary_quarter;
        high_ -= binary_quarter;
        code_ -= binary_quarter;
      } else {
        break;
      }
      low_ <<= 1U;
      high_ = (high_ << 1U) | 1U;
      code_ = (code_ << 1U) | read_bit();
    }
    return bit;
  }

  [[nodiscard]] auto decode_byte(binary_model const &probabilities) -> std::byte
  {
    unsigned byte = 0;
    for (unsigned index = 0; index < support::bits_per_byte; ++index) {
      byte = (byte << 1U) | static_cast<unsigned>(decode(probabilities));
    }
    return static_cast<std::byte>(byte);
  }

  //! Call after the externally known bit count. Verify the canonical terminal
  //! code and at most seven zero padding bits, rejecting trailing bytes.
  void finish()
  {
    require_active();
    auto const terminal = low_ < binary_quarter ? binary_quarter : binary_half;
    if (code_ != terminal || position_ != payload_.size() - (used_bits_ != 0 ? 1U : 0U)) { invalid_payload(); }
    while (used_bits_ != 0) {
      if (read_bit() != 0) { invalid_payload(); }
    }
    finished_ = true;
  }

private:
  [[noreturn]] static void invalid_payload()
  { throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid or truncated binary arithmetic payload"); }

  void require_active() const
  {
    if (finished_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Binary arithmetic decoder already finished");
    }
  }

  auto read_bit() -> std::uint32_t
  {
    if (position_ == payload_.size()) { invalid_payload(); }
    auto const bit = (std::to_integer<unsigned>(payload_[position_]) >> (support::bits_per_byte - 1 - used_bits_)) & 1U;
    if (++used_bits_ == support::bits_per_byte) {
      used_bits_ = 0;
      ++position_;
    }
    return bit;
  }

  std::span<std::byte const> payload_;
  std::size_t position_ = 0;
  unsigned used_bits_ = 0;
  std::uint32_t low_ = 0;
  std::uint32_t high_ = binary_initial_high;
  std::uint32_t code_ = 0;
  bool finished_ = false;
};
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_DECODER_HPP
