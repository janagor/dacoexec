//! \file
//! Alternative adaptive byte model with logarithmic prefix sums and updates.
#ifndef DACOEXEC_ARITHMETIC_FENWICK_ADAPTIVE_MODEL_HPP
#define DACOEXEC_ARITHMETIC_FENWICK_ADAPTIVE_MODEL_HPP

#include <dacoexec/detail/arithmetic/adaptive_model_common.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dacoexec::detail::arithmetic {
class fenwick_adaptive_model
{
public:
  using table_type = adaptive_frequency_table;
  static constexpr std::uint32_t rescale_threshold = adaptive_rescale_threshold;

  fenwick_adaptive_model()
  {
    frequencies_.fill(1);
    rebuild_tree();
  }

  [[nodiscard]] auto frequencies() const noexcept -> table_type const & { return frequencies_; }

  [[nodiscard]] auto total() const noexcept -> std::uint32_t { return total_; }

  [[nodiscard]] auto interval_for(std::uint8_t symbol) const -> adaptive_interval
  {
    std::uint32_t cumulative = 0;
    // One-based tree: prefix(symbol) excludes the symbol's own frequency.
    for (std::size_t index = symbol; index != 0; index -= low_bit(index)) { cumulative += tree_.at(index); }
    return { cumulative, frequencies_.at(symbol), total_ };
  }

  [[nodiscard]] auto find_symbol(std::uint32_t slot) const -> std::uint8_t
  {
    validate_adaptive_slot(slot, total_);
    std::size_t index = 0;
    // Binary lifting finds the largest prefix sum <= slot in O(log 256).
    // The alphabet is a power of two; slot < total keeps index below 256.
    for (std::size_t step = model::alphabet_size; step != 0; step >>= 1U) {
      auto const next = index + step;
      if (next <= model::alphabet_size && tree_.at(next) <= slot) {
        slot -= tree_.at(next);
        index = next;
      }
    }
    return static_cast<std::uint8_t>(index);
  }

  //! Use the current interval before updating, exactly as for adaptive_model.
  void update(std::uint8_t symbol)
  {
    ++frequencies_.at(symbol);
    ++total_;
    if (total_ > rescale_threshold) {
      rescale();
    } else {
      for (std::size_t index = std::size_t{ symbol } + 1; index <= model::alphabet_size; index += low_bit(index)) {
        ++tree_.at(index);
      }
    }
  }

  void rescale()
  {
    rescale_adaptive_frequencies(frequencies_);
    rebuild_tree();
  }

private:
  [[nodiscard]] static constexpr auto low_bit(std::size_t index) noexcept -> std::size_t
  { return index & (~index + 1U); }

  void rebuild_tree()
  {
    tree_.fill(0);
    total_ = 0;
    // Linear rebuild: each completed subtree contributes once to its parent.
    for (std::size_t index = 1; index <= model::alphabet_size; ++index) {
      auto const frequency = frequencies_.at(index - 1);
      total_ += frequency;
      tree_.at(index) += frequency;
      auto const parent = index + low_bit(index);
      if (parent <= model::alphabet_size) { tree_.at(parent) += tree_.at(index); }
    }
  }

  table_type frequencies_{};
  std::array<std::uint32_t, model::alphabet_size + 1> tree_{};
  std::uint32_t total_ = 0;
};

[[nodiscard]] inline auto find_symbol(fenwick_adaptive_model const &probabilities, std::uint32_t slot) -> std::uint8_t
{ return probabilities.find_symbol(slot); }
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_FENWICK_ADAPTIVE_MODEL_HPP
