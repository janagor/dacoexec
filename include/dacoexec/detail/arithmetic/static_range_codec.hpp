//! \file
//! Static order-0 block adapter; model, payload, and symbol count are separate.
#ifndef DACOEXEC_ARITHMETIC_STATIC_RANGE_CODEC_HPP
#define DACOEXEC_ARITHMETIC_STATIC_RANGE_CODEC_HPP

#include <dacoexec/detail/arithmetic/range_decoder.hpp>
#include <dacoexec/detail/arithmetic/range_encoder.hpp>
#include <dacoexec/detail/arithmetic/static_model.hpp>
#include <dacoexec/detail/support/byte.hpp>

#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

namespace dacoexec::detail::arithmetic {
//! The caller counts once before encoding, or supplies an existing fixed model.
//! The source is consumed once, in forward order, without buffering it.
template<byte_input_range R>
[[nodiscard]] auto encode_block(R &&source, static_model const &probabilities) -> std::vector<std::byte>
{
  range_encoder encoder;
  for (auto &&value : source) {
    using source_value = std::ranges::range_value_t<R>;
    auto const symbol = std::to_integer<unsigned char>(support::to_byte(static_cast<source_value>(value)));
    encoder.encode(
      probabilities.cumulative().at(symbol), probabilities.frequencies().at(symbol), probabilities.total());
  }
  return encoder.finish();
}

//! Decode exactly count symbols and validate range-engine termination. The
//! explicit output limit is checked before allocation. A fresh engine resets each block.
[[nodiscard]] inline auto decode_block(std::span<std::byte const> payload,
  static_model const &probabilities,
  std::size_t count,
  std::size_t limit) -> std::vector<std::byte>
{
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "Static range output exceeds limit");
  }
  range_decoder decoder(payload);
  std::vector<std::byte> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto const symbol = find_symbol(probabilities, decoder.cumulative(probabilities.total()));
    decoder.decode(
      probabilities.cumulative().at(symbol), probabilities.frequencies().at(symbol), probabilities.total());
    result.push_back(static_cast<std::byte>(symbol));
  }
  decoder.finish();
  return result;
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_STATIC_RANGE_CODEC_HPP
