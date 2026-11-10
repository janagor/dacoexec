//! \file
//! Deterministic adaptive order-0 byte probabilities for the range engine.
#ifndef DACOEXEC_ARITHMETIC_ADAPTIVE_MODEL_HPP
#define DACOEXEC_ARITHMETIC_ADAPTIVE_MODEL_HPP

#include <dacoexec/detail/arithmetic/adaptive_model_common.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace dacoexec::detail::arithmetic {
class adaptive_model
{
public:
  using table_type = adaptive_frequency_table;
  static constexpr std::uint32_t rescale_threshold = adaptive_rescale_threshold;

  //! Construct a fresh frame model; every byte starts with frequency one.
  adaptive_model()
  {
    frequencies_.fill(1);
    rebuild_cumulative();
  }

  [[nodiscard]] auto frequencies() const noexcept -> table_type const & { return frequencies_; }

  [[nodiscard]] auto cumulative() const noexcept -> table_type const & { return cumulative_; }

  [[nodiscard]] auto total() const noexcept -> std::uint32_t { return total_; }

  [[nodiscard]] auto interval_for(std::uint8_t symbol) const -> adaptive_interval
  { return { cumulative_.at(symbol), frequencies_.at(symbol), total_ }; }

  [[nodiscard]] auto find_symbol(std::uint32_t slot) const -> std::uint8_t
  {
    validate_adaptive_slot(slot, total_);
    return static_cast<std::uint8_t>(
      std::upper_bound(cumulative_.begin(), cumulative_.end(), slot) - cumulative_.begin() - 1);
  }

  //! Call exactly once AFTER encoding or decoding with the current interval.
  //! Equality does not rescale: only a total above the threshold triggers it.
  void update(std::uint8_t symbol)
  {
    ++frequencies_.at(symbol);
    ++total_;
    if (total_ > rescale_threshold) {
      rescale();
    } else {
      for (std::size_t next = std::size_t{ symbol } + 1; next < model::alphabet_size; ++next) {
        ++cumulative_.at(next);
      }
    }
  }

  //! Round each positive frequency upward when halving, preserving unseen bytes.
  void rescale()
  {
    rescale_adaptive_frequencies(frequencies_);
    rebuild_cumulative();
  }

private:
  void rebuild_cumulative()
  {
    total_ = 0;
    for (std::size_t symbol = 0; symbol < model::alphabet_size; ++symbol) {
      cumulative_.at(symbol) = total_;
      total_ += frequencies_.at(symbol);
    }
  }

  table_type frequencies_{};
  table_type cumulative_{};
  std::uint32_t total_ = 0;
};

//! Find the positive-width interval containing slot in the current model.
[[nodiscard]] inline auto find_symbol(adaptive_model const &probabilities, std::uint32_t slot) -> std::uint8_t
{ return probabilities.find_symbol(slot); }
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_ADAPTIVE_MODEL_HPP
