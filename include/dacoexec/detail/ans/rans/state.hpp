//! \file
//! Single-state byte-renormalized rANS engine.
#ifndef DACOEXEC_ANS_RANS_STATE_HPP
#define DACOEXEC_ANS_RANS_STATE_HPP

#include <algorithm>
#include <dacoexec/detail/ans/model.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace dacoexec::detail::ans::rans {
inline constexpr std::uint32_t lower_bound = 1U << 23U;
inline constexpr unsigned byte_bits = dacoexec::detail::support::bits_per_byte;
inline constexpr std::uint32_t radix = 1U << byte_bits;
inline constexpr std::uint32_t byte_mask = radix - 1;
inline constexpr std::size_t state_bytes = sizeof(std::uint32_t);

template<std::ranges::bidirectional_range R, std::size_t symbol_count>
[[nodiscard]] auto encode(R &&source, frequency_model<symbol_count> const &model) -> std::vector<std::byte>
{
  static_assert(symbol_count <= alphabet_size);
  std::uint32_t state = lower_bound;
  std::vector<std::byte> output(state_bytes);
  auto &&input = std::forward<R>(source);
  auto const first = std::ranges::begin(input);
  auto cursor = std::ranges::next(first, std::ranges::end(input));
  while (cursor != first) {
    --cursor;
    using source_value = std::ranges::range_value_t<R>;
    auto const symbol =
      std::to_integer<std::uint8_t>(dacoexec::detail::support::to_byte(static_cast<source_value>(*cursor)));
    auto const frequency = model.frequencies().at(symbol);
    if (frequency == 0) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "ANS symbol absent from model");
    }
    auto const threshold = (std::uint64_t{ lower_bound } >> model.precision_bits()) * radix * frequency;
    while (state >= threshold) {
      output.push_back(static_cast<std::byte>(state & byte_mask));
      state >>= byte_bits;
    }
    state = ((state / frequency) << model.precision_bits()) + (state % frequency) + model.cumulative().at(symbol);
  }
  auto const bytes = dacoexec::detail::support::write_u32_le(state);
  std::ranges::reverse(std::ranges::subrange(output.begin() + state_bytes, output.end()));
  std::ranges::copy(bytes, output.begin());
  return output;
}

template<class Symbol = std::uint8_t, std::size_t symbol_count>
[[nodiscard]] auto decode(std::span<std::byte const> payload,
  frequency_model<symbol_count> const &model,
  std::size_t count,
  std::size_t limit) -> std::vector<Symbol>
{
  static_assert(symbol_count <= alphabet_size);
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "ANS output exceeds configured limit");
  }
  if (payload.size() < state_bytes) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid ANS size");
  }
  auto state = dacoexec::detail::support::read_u32_le(payload.first<state_bytes>());
  if (state < lower_bound || state >= std::uint64_t{ lower_bound } * radix) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid ANS initial state");
  }
  std::vector<std::uint8_t> lookup(model.total());
  for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
    std::fill_n(lookup.begin() + model.cumulative().at(symbol),
      model.frequencies().at(symbol),
      static_cast<std::uint8_t>(symbol));
  }
  std::vector<Symbol> output;
  output.reserve(count);
  auto position = state_bytes;
  for (std::size_t index = 0; index < count; ++index) {
    auto const slot = state & (model.total() - 1);
    auto const symbol = lookup.at(slot);
    output.push_back(static_cast<Symbol>(symbol));
    state = (model.frequencies().at(symbol) * (state >> model.precision_bits())) + slot - model.cumulative().at(symbol);
    while (state < lower_bound) {
      if (position == payload.size()) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Truncated ANS payload");
      }
      state = (state << byte_bits) | std::to_integer<std::uint32_t>(payload.subspan(position++).front());
    }
  }
  if (position != payload.size() || state != lower_bound) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid ANS terminal state or trailing bytes");
  }
  return output;
}
}// namespace dacoexec::detail::ans::rans
#endif// DACOEXEC_ANS_RANS_STATE_HPP
