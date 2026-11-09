//! \file
//! Immutable integer probability models for the ANS family.
#ifndef DACOEXEC_ANS_MODEL_HPP
#define DACOEXEC_ANS_MODEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/ans/model_normalization.hpp>
#include <dacoexec/detail/codec/frequency.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::ans {
using codec::alphabet_size;
inline constexpr unsigned default_precision_bits = 12;
inline constexpr unsigned max_precision_bits = 16;
using codec::frequency_table;

template<std::size_t symbol_count> class frequency_model
{
public:
  using table_type = std::array<std::uint32_t, symbol_count>;

  explicit frequency_model(table_type frequencies, unsigned precision = default_precision_bits)
    : frequencies_(frequencies), precision_bits_(precision)
  {
    if (precision == 0 || precision > max_precision_bits) { throw std::invalid_argument("Invalid ANS precision"); }
    std::uint64_t sum = 0;
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
      cumulative_.at(symbol) = static_cast<std::uint32_t>(sum);
      sum += frequencies_.at(symbol);
      if (sum > total()) { throw std::invalid_argument("Invalid ANS frequency sum"); }
    }
    if (sum != total()) { throw std::invalid_argument("Invalid ANS frequency sum"); }
  }

  [[nodiscard]] auto frequencies() const noexcept -> table_type const & { return frequencies_; }

  [[nodiscard]] auto cumulative() const noexcept -> table_type const & { return cumulative_; }

  [[nodiscard]] auto precision_bits() const noexcept -> unsigned { return precision_bits_; }

  [[nodiscard]] auto total() const noexcept -> std::uint32_t { return std::uint32_t{ 1 } << precision_bits_; }

private:
  table_type frequencies_;
  table_type cumulative_{};
  unsigned precision_bits_;
};

// Reserve one slot for each observed symbol, then apportion the remaining
// slots proportionally. Binary long division avoids overflowing count * slots.
template<std::size_t symbol_count>
[[nodiscard]] auto make_model(std::array<std::uint64_t, symbol_count> const &counts,
  unsigned precision = default_precision_bits) -> frequency_model<symbol_count>
{
  if (precision == 0 || precision > max_precision_bits) { throw std::invalid_argument("Invalid ANS precision"); }
  std::uint64_t sum = 0;
  std::uint32_t present = 0;
  for (auto count : counts) {
    if (count > std::numeric_limits<std::uint64_t>::max() - sum) { throw std::overflow_error("ANS count overflow"); }
    sum += count;
    present += count != 0 ? 1U : 0U;
  }
  auto const total = std::uint32_t{ 1 } << precision;
  if (sum == 0 || present > total) { throw std::invalid_argument("ANS model cannot represent alphabet"); }
  auto const slots = total - present;
  typename frequency_model<symbol_count>::table_type frequencies{};
  std::array<std::uint64_t, symbol_count> remainders{};
  std::uint32_t assigned = 0;
  for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
    auto const count = counts.at(symbol);
    if (count == 0) { continue; }
    auto const [quotient, remainder] = ::dacoexec::detail::ans::proportional_share(
      { .count_ = count, .slots_ = slots, .sum_ = sum, .precision_ = precision });
    frequencies.at(symbol) = quotient + 1;
    remainders.at(symbol) = remainder;
    assigned += quotient + 1;
  }
  while (assigned < total) {
    std::size_t best = 0;
    for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
      if (remainders.at(symbol) > remainders.at(best)) { best = symbol; }
    }
    ++frequencies.at(best);
    remainders.at(best) = 0;
    ++assigned;
  }
  return frequency_model<symbol_count>{ frequencies, precision };
}

[[nodiscard]] inline auto make_binary_model(std::uint64_t zeros,
  std::uint64_t ones,
  unsigned precision = default_precision_bits) -> frequency_model<2>
{ return make_model(std::array<std::uint64_t, 2>{ zeros, ones }, precision); }

template<byte_input_range R> [[nodiscard]] constexpr auto count_frequencies(R &&source) -> frequency_table
{ return codec::count_frequencies(std::forward<R>(source), "ANS count overflow"); }
}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_MODEL_HPP
