//! \file
//! Adaptive binary probabilities selected by bit position and two-bit history.
#ifndef DACOEXEC_ARITHMETIC_BINARY_CONTEXT_MODEL_HPP
#define DACOEXEC_ARITHMETIC_BINARY_CONTEXT_MODEL_HPP

#include <dacoexec/detail/arithmetic/binary/model.hpp>
#include <dacoexec/detail/support/byte.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dacoexec::detail::arithmetic {
class context_model
{
public:
  static constexpr std::size_t context_count = 32;
  //! Fixed model convention, independent of the byte-oriented range coder.
  static constexpr std::uint32_t rescale_threshold = 1U << 15U;
  using table_type = std::array<binary_model, context_count>;
  static_assert(rescale_threshold + 1 <= binary_max_total);

  //! Position zero is the MSB. History starts at zero and persists across bytes.
  [[nodiscard]] auto context_index() const noexcept -> std::size_t
  { return (bit_position_ << history_bits) | history_; }

  //! Use these probabilities BEFORE calling update with the coded/decoded bit.
  //! Fixed-model encode_byte/decode_byte helpers cannot update this per-bit policy.
  [[nodiscard]] auto probabilities() const -> binary_model const & { return contexts_.at(context_index()); }

  [[nodiscard]] auto contexts() const noexcept -> table_type const & { return contexts_; }

  //! Update only the selected context, then advance history and byte position.
  //! Rescale strictly above the threshold, rounding positive counts upward.
  void update(bool bit)
  {
    auto &current = contexts_.at(context_index());
    if (bit) {
      ++current.one_frequency_;
    } else {
      ++current.zero_frequency_;
    }
    if (current.zero_frequency_ + current.one_frequency_ > rescale_threshold) {
      current.zero_frequency_ = (current.zero_frequency_ + 1U) / 2U;
      current.one_frequency_ = (current.one_frequency_ + 1U) / 2U;
    }
    history_ = ((history_ << 1U) | static_cast<unsigned>(bit)) & history_mask;
    bit_position_ = (bit_position_ + 1) % support::bits_per_byte;
  }

  //! Begin a new frame with uniform probabilities, position zero, and no history.
  void reset() noexcept
  {
    contexts_ = {};
    bit_position_ = 0;
    history_ = 0;
  }

private:
  static constexpr unsigned history_bits = 2;
  static constexpr unsigned history_mask = (1U << history_bits) - 1U;
  table_type contexts_{};
  std::size_t bit_position_ = 0;
  unsigned history_ = 0;
};
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_CONTEXT_MODEL_HPP
