//! \file
//! Integer binary arithmetic encoder; see docs/binary-arithmetic-coding.md.
#ifndef DACOEXEC_ARITHMETIC_BINARY_ENCODER_HPP
#define DACOEXEC_ARITHMETIC_BINARY_ENCODER_HPP

#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/binary/common.hpp>
#include <utility>
#include <vector>

namespace dacoexec::detail::arithmetic {
class binary_arithmetic_encoder
{
public:
  //! Models may vary between calls; encoder and decoder must use identical ones.
  void encode(bool bit, binary_model const &probabilities)
  {
    require_active();
    auto const split = binary_split(low_, high_, probabilities);
    if (bit) {
      low_ = split;
    } else {
      high_ = split - 1;
    }
    for (;;) {
      if (high_ < binary_half) {
        emit_with_pending(false);// E1: common leading zero.
      } else if (low_ >= binary_half) {
        emit_with_pending(true);// E2: common leading one.
        low_ -= binary_half;
        high_ -= binary_half;
      } else if (low_ >= binary_quarter && high_ < binary_three_quarters) {
        add_pending();// E3: postpone the ambiguous bit and its complement.
        low_ -= binary_quarter;
        high_ -= binary_quarter;
      } else {
        break;
      }
      low_ <<= 1U;
      high_ = (high_ << 1U) | 1U;
    }
  }

  //! Expand a source byte from bit 7 through bit 0.
  void encode_byte(std::byte byte, binary_model const &probabilities)
  {
    for (unsigned remaining = support::bits_per_byte; remaining != 0; --remaining) {
      encode((std::to_integer<unsigned>(byte) & (1U << (remaining - 1))) != 0, probabilities);
    }
  }

  //! Emit the terminating decision and deferred complements, 30 zero guard
  //! bits, then zero padding to a byte boundary. May be called exactly once.
  [[nodiscard]] auto finish() -> std::vector<std::byte>
  {
    require_active();
    add_pending();
    emit_with_pending(low_ >= binary_quarter);
    for (unsigned index = 0; index < binary_state_bits - 2; ++index) { write_bit(false); }
    while (used_bits_ != 0) { write_bit(false); }
    finished_ = true;
    return std::move(output_);
  }

private:
  void add_pending()
  {
    if (pending_bits_ == std::numeric_limits<std::uint64_t>::max()) {
      throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "Binary arithmetic pending bit count overflow");
    }
    ++pending_bits_;
  }

  void require_active() const
  {
    if (finished_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Binary arithmetic encoder already finished");
    }
  }

  void write_bit(bool bit)
  {
    if (used_bits_ == 0) { output_.push_back(std::byte{ 0 }); }
    if (bit) { output_.back() |= static_cast<std::byte>(1U << (support::bits_per_byte - 1 - used_bits_)); }
    used_bits_ = (used_bits_ + 1) % support::bits_per_byte;
  }

  void emit_with_pending(bool bit)
  {
    write_bit(bit);
    while (pending_bits_ != 0) {
      write_bit(!bit);
      --pending_bits_;
    }
  }

  std::uint32_t low_ = 0;
  std::uint32_t high_ = binary_initial_high;
  std::uint64_t pending_bits_ = 0;
  std::vector<std::byte> output_;
  unsigned used_bits_ = 0;
  bool finished_ = false;
};
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_ENCODER_HPP
