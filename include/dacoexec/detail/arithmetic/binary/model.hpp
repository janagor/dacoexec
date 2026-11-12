//! \file
//! Fixed, smoothed binary probabilities for a complete frame.
#ifndef DACOEXEC_ARITHMETIC_BINARY_MODEL_HPP
#define DACOEXEC_ARITHMETIC_BINARY_MODEL_HPP

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/model/frequency_model.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>
#include <limits>
#include <ranges>

namespace dacoexec::detail::arithmetic {
inline constexpr std::uint32_t binary_max_total = 1U << 16U;

struct binary_model
{
  std::uint32_t zero_frequency_ = 1;
  std::uint32_t one_frequency_ = 1;

  //! Both outcomes must have positive width. Check before adding to avoid overflow.
  [[nodiscard]] auto total() const -> std::uint32_t
  {
    if (zero_frequency_ == 0 || one_frequency_ == 0 || zero_frequency_ >= binary_max_total
        || one_frequency_ > binary_max_total - zero_frequency_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid binary probability model");
    }
    return zero_frequency_ + one_frequency_;
  }
};

//! Count all bits once, add one pseudocount to each outcome, then reuse the
//! shared integer normalization (default total 4096). Empty input is uniform.
//! Invalid precision is rejected before accessing input, including single-pass ranges.
template<byte_input_range R>
[[nodiscard]] auto make_binary_model(R &&input, unsigned precision = model::default_precision_bits) -> binary_model
{
  if (precision == 0 || precision > model::max_precision_bits) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid model precision");
  }
  std::array<std::uint64_t, 2> counts{ 1, 1 };
  for (auto value : input) {
    using source_value = std::ranges::range_value_t<R>;
    auto const byte = support::to_byte(static_cast<source_value>(value));
    auto const ones = static_cast<unsigned>(std::popcount(std::to_integer<unsigned char>(byte)));
    std::array const additions{ support::bits_per_byte - ones, ones };
    for (std::size_t index = 0; index < counts.size(); ++index) {
      if (counts.at(index) > std::numeric_limits<std::uint64_t>::max() - additions.at(index)) {
        throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "Binary frequency counter overflow");
      }
      counts.at(index) += additions.at(index);
    }
  }
  auto const normalized = model::make_model(counts, precision);
  return { normalized.frequencies().at(0), normalized.frequencies().at(1) };
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_MODEL_HPP
