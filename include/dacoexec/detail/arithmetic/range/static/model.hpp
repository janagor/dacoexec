//! \file
//! Static byte probabilities and standalone model metadata for range coding.
#ifndef DACOEXEC_ARITHMETIC_STATIC_MODEL_HPP
#define DACOEXEC_ARITHMETIC_STATIC_MODEL_HPP

#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/model/frequency.hpp>
#include <dacoexec/detail/model/frequency_model.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace dacoexec::detail::arithmetic {
using static_model = model::frequency_model<model::alphabet_size>;
inline constexpr std::size_t static_model_min_metadata_size = 6;// Precision, count, and one entry.

//! Optional direct cumulative-slot lookup: 2^precision bytes, built per model.
[[nodiscard]] inline auto make_symbol_lookup(static_model const &probabilities) -> std::vector<std::uint8_t>
{
  std::vector<std::uint8_t> result(probabilities.total());
  for (std::size_t symbol = 0; symbol < model::alphabet_size; ++symbol) {
    auto const first = probabilities.cumulative().at(symbol);
    auto const last = first + probabilities.frequencies().at(symbol);
    for (auto slot = first; slot < last; ++slot) { result.at(slot) = static_cast<std::uint8_t>(symbol); }
  }
  return result;
}

//! Empty input has no probability distribution and reports invalid_argument.
template<byte_input_range R>
[[nodiscard]] auto make_static_model(R &&input, unsigned precision = model::default_precision_bits) -> static_model
{ return model::make_model(model::count_frequencies(std::forward<R>(input)), precision); }

//! Find the positive-width interval containing slot, skipping absent symbols.
[[nodiscard]] inline auto find_symbol(static_model const &probabilities, std::uint32_t slot) -> std::uint8_t
{
  if (slot >= probabilities.total()) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Cumulative slot outside static model");
  }
  auto const &cumulative = probabilities.cumulative();
  return static_cast<std::uint8_t>(
    std::upper_bound(cumulative.begin(), cumulative.end(), slot) - cumulative.begin() - 1);
}

//! ANS-style metadata: precision byte, LE uint16 entry count, then sorted
//! (symbol byte, LE uint16 frequency-minus-one) entries. No framing or payload.
[[nodiscard]] inline auto serialize_model(static_model const &probabilities) -> std::vector<std::byte>
{
  std::vector<std::byte> bytes;
  auto const count = std::ranges::count_if(probabilities.frequencies(), [](auto frequency) { return frequency != 0; });
  bytes.reserve(sizeof(std::uint8_t) + sizeof(std::uint16_t)
                + static_cast<std::size_t>(count) * (sizeof(std::uint8_t) + sizeof(std::uint16_t)));
  bytes.push_back(static_cast<std::byte>(probabilities.precision_bits()));
  auto const append_u16 = [&bytes](std::uint16_t value) {
    auto const encoded = support::write_u16_le(value);
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
  };
  append_u16(static_cast<std::uint16_t>(count));
  for (std::size_t symbol = 0; symbol < model::alphabet_size; ++symbol) {
    auto const frequency = probabilities.frequencies().at(symbol);
    if (frequency == 0) { continue; }
    bytes.push_back(static_cast<std::byte>(symbol));
    append_u16(static_cast<std::uint16_t>(frequency - 1));
  }
  return bytes;
}

//! Consume exactly one metadata span. Invalid, truncated, or trailing metadata
//! reports invalid_payload; validation happens before constructing a model.
[[nodiscard]] inline auto deserialize_model(std::span<std::byte const> bytes) -> static_model
{
  auto const invalid = [] {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid static range model metadata");
  };
  constexpr std::size_t prefix_size = 3;
  constexpr std::size_t entry_size = 3;
  if (bytes.size() < prefix_size) { invalid(); }
  auto const precision = std::to_integer<unsigned>(bytes.front());
  auto const count = support::read_u16_le(std::span<std::byte const, 2>{ bytes.subspan(1, 2) });
  if (precision == 0 || precision > model::max_precision_bits || count == 0 || count > model::alphabet_size
      || bytes.size() != prefix_size + entry_size * count) {
    invalid();
  }
  static_model::table_type frequencies{};
  std::uint32_t sum = 0;
  auto const total = std::uint32_t{ 1 } << precision;
  unsigned previous = 0;
  for (std::size_t entry = 0; entry < count; ++entry) {
    auto const offset = prefix_size + entry * entry_size;
    auto const symbol = std::to_integer<unsigned>(bytes.subspan(offset).front());
    if (entry != 0 && symbol <= previous) { invalid(); }
    previous = symbol;
    auto const frequency =
      std::uint32_t{ support::read_u16_le(std::span<std::byte const, 2>{ bytes.subspan(offset + 1, 2) }) } + 1;
    if (frequency > total - sum) { invalid(); }
    sum += frequency;
    frequencies.at(symbol) = frequency;
  }
  if (sum != total) { invalid(); }
  try {
    return static_model{ frequencies, precision };
  } catch (::dacoexec::exception const &error) {
    // Keep the wire deserializer's error contract independent of constructor
    // validation, which reports invalid user-supplied models as argument errors.
    if (error.code() != errc::invalid_argument) { throw; }
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, error.what());
  }
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_STATIC_MODEL_HPP
