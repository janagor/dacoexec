//! \file
//! Carry-safe byte-oriented integer range encoder; see docs/range-coding.md.
#ifndef DACOEXEC_ARITHMETIC_RANGE_ENCODER_HPP
#define DACOEXEC_ARITHMETIC_RANGE_ENCODER_HPP

#include <dacoexec/detail/arithmetic/range_common.hpp>
#include <utility>
#include <vector>

namespace dacoexec::detail::arithmetic {
class range_encoder
{
public:
  //! Encode [cumulative, cumulative + frequency) in a caller-supplied model.
  void encode(std::uint32_t cumulative, std::uint32_t frequency, std::uint32_t total)
  {
    require_active();
    validate_range_interval(cumulative, frequency, total);
    auto const unit = range_ / total;
    low_ += std::uint64_t{ unit } * cumulative;
    range_ = unit * frequency;
    while (range_ < range_normalization) {
      range_ <<= range_byte_bits;
      shift_low();
    }
  }

  //! Flush exactly five shifts and transfer the payload. May be called once.
  [[nodiscard]] auto finish() -> std::vector<std::byte>
  {
    require_active();
    for (std::size_t index = 0; index < range_flush_bytes; ++index) { shift_low(); }
    finished_ = true;
    return std::move(output_);
  }

private:
  void require_active() const
  {
    if (finished_) { throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Range encoder already finished"); }
  }

  void shift_low()
  {
    auto const low = static_cast<std::uint32_t>(low_);
    auto const carry = static_cast<std::uint32_t>(low_ >> 32U);
    // A leading 0xFF is ambiguous until the next carry is known. Cache it and
    // propagate that carry through the entire pending run before emitting.
    if (low < (range_byte_mask << 24U) || carry != 0) {
      auto byte = cache_;
      do {
        output_.push_back(static_cast<std::byte>((byte + carry) & range_byte_mask));
        byte = range_byte_mask;
      } while (--pending_ != 0);
      cache_ = low >> 24U;
    }
    ++pending_;
    // Discard shifted-out high bits: they have already been cached above.
    low_ = (std::uint64_t{ low } << range_byte_bits) & range_initial;
  }

  std::uint64_t low_ = 0;
  std::uint32_t range_ = range_initial;
  std::uint32_t cache_ = 0;
  std::size_t pending_ = 1;
  std::vector<std::byte> output_;
  bool finished_ = false;
};
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_RANGE_ENCODER_HPP
