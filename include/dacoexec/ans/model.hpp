//! \file
//! Immutable integer probability models for the ANS family.
#ifndef DACOEXEC_ANS_MODEL_HPP
#define DACOEXEC_ANS_MODEL_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dacoexec::ans {
inline constexpr std::size_t alphabet_size = 256;
inline constexpr unsigned default_precision_bits = 12;
inline constexpr unsigned max_precision_bits = 16;
using frequency_table = std::array<std::uint64_t, alphabet_size>;

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

namespace detail {
struct frequency_share
{
  std::uint32_t quotient_;
  std::uint64_t remainder_;
};

struct proportional_share_parameters
{
  std::uint64_t count_;
  std::uint32_t slots_;
  std::uint64_t sum_;
  unsigned precision_;
};

// Computes count * slots / sum and its remainder without forming the product.
// The caller supplies count <= sum, sum > 0, and slots <= 2^precision.
[[nodiscard]] inline auto proportional_share(proportional_share_parameters parameters) -> frequency_share
{
  auto const [count, slots, sum, precision] = parameters;
  std::uint64_t remainder = 0;
  std::uint32_t quotient = 0;
  for (unsigned bit = precision + 1; bit != 0; --bit) {
    quotient *= 2;
    if (remainder >= sum - remainder) { remainder -= sum - remainder; ++quotient; }
    else { remainder += remainder; }
    if (((slots >> (bit - 1)) & 1U) != 0) {
      if (remainder >= sum - count) { remainder -= sum - count; ++quotient; }
      else { remainder += count; }
    }
  }
  return { .quotient_ = quotient, .remainder_ = remainder };
}
}// namespace detail

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
    auto const [quotient, remainder] = detail::proportional_share(
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

[[nodiscard]] inline auto make_binary_model(std::uint64_t zeros, std::uint64_t ones,
  unsigned precision = default_precision_bits) -> frequency_model<2>
{ return make_model(std::array<std::uint64_t, 2>{ zeros, ones }, precision); }

template<byte_input_range R> [[nodiscard]] auto count_frequencies(R &&source) -> frequency_table
{
  frequency_table counts{};
  for (auto value : std::forward<R>(source)) {
    using source_value = std::ranges::range_value_t<R>;
    auto &count = counts.at(std::to_integer<unsigned>(dacoexec::detail::to_byte(static_cast<source_value>(value))));
    if (count == std::numeric_limits<std::uint64_t>::max()) { throw std::overflow_error("ANS count overflow"); }
    ++count;
  }
  return counts;
}
}// namespace dacoexec::ans
#endif// DACOEXEC_ANS_MODEL_HPP
