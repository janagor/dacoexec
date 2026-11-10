//! \file
//! Byte-oriented range decoder; the caller supplies models and symbol count.
#ifndef DACOEXEC_ARITHMETIC_RANGE_DECODER_HPP
#define DACOEXEC_ARITHMETIC_RANGE_DECODER_HPP

#include <dacoexec/detail/arithmetic/range_common.hpp>
#include <span>

namespace dacoexec::detail::arithmetic {
class range_decoder
{
public:
  //! The borrowed payload must remain valid throughout decoding.
  explicit range_decoder(std::span<std::byte const> payload) : payload_(payload)
  {
    if (read_byte() != 0) { invalid_payload(); }
    for (std::size_t index = 1; index < range_flush_bytes; ++index) {
      code_ = (code_ << range_byte_bits) | read_byte();
    }
    if (code_ >= range_) { invalid_payload(); }
  }

  //! Return a cumulative slot without changing the state. Look up its symbol
  //! in the model, then call decode() with that symbol's interval and total.
  [[nodiscard]] auto cumulative(std::uint32_t total) const -> std::uint32_t
  {
    require_active();
    validate_range_total(total);
    auto const slot = code_ / (range_ / total);
    if (slot >= total) { invalid_payload(); }
    return slot;
  }

  void decode(std::uint32_t cumulative, std::uint32_t frequency, std::uint32_t total)
  {
    require_active();
    validate_range_interval(cumulative, frequency, total);
    auto const unit = range_ / total;
    auto const start = unit * cumulative;
    auto const size = unit * frequency;
    if (code_ < start || code_ - start >= size) { invalid_payload(); }
    code_ -= start;
    range_ = size;
    while (range_ < range_normalization) {
      range_ <<= range_byte_bits;
      code_ = (code_ << range_byte_bits) | read_byte();
    }
  }

  //! Call after the externally known symbol count. Reject unconsumed bytes
  //! and a nonzero terminal code. This is a termination check, not a checksum.
  void finish()
  {
    require_active();
    if (position_ != payload_.size() || code_ != 0) { invalid_payload(); }
    finished_ = true;
  }

private:
  [[noreturn]] static void invalid_payload()
  { throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid or truncated range payload"); }

  void require_active() const
  {
    if (finished_) { throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Range decoder already finished"); }
  }

  auto read_byte() -> std::uint32_t
  {
    if (position_ == payload_.size()) { invalid_payload(); }
    return std::to_integer<std::uint32_t>(payload_.subspan(position_++).front());
  }

  std::span<std::byte const> payload_;
  std::size_t position_ = 0;
  std::uint32_t range_ = range_initial;
  std::uint32_t code_ = 0;
  bool finished_ = false;
};
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_RANGE_DECODER_HPP
