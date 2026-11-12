//! Adaptive order-0 adapter over the shared byte-oriented range engine.
#ifndef DACOEXEC_ARITHMETIC_ADAPTIVE_RANGE_CODEC_HPP
#define DACOEXEC_ARITHMETIC_ADAPTIVE_RANGE_CODEC_HPP

#include <cstddef>
#include <dacoexec/detail/arithmetic/range/adaptive/model.hpp>
#include <dacoexec/detail/arithmetic/range/decoder.hpp>
#include <dacoexec/detail/arithmetic/range/encoder.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <ranges>
#include <span>
#include <vector>

namespace dacoexec::detail::arithmetic {
//! A fresh uniform model is updated after each symbol and reset per block.
//! Model selection is internal: equivalent representations share one wire format.
template<class Model = adaptive_model, byte_input_range R>
[[nodiscard]] auto encode_adaptive_block(R &&source) -> std::vector<std::byte>
{
  Model probabilities;
  range_encoder encoder;
  for (auto &&value : source) {
    using source_value = std::ranges::range_value_t<R>;
    auto const symbol = std::to_integer<unsigned char>(support::to_byte(static_cast<source_value>(value)));
    auto const interval = probabilities.interval_for(symbol);
    encoder.encode(interval.cumulative_, interval.frequency_, interval.total_);
    probabilities.update(symbol);
  }
  return encoder.finish();
}

template<class Model = adaptive_model>
[[nodiscard]] inline auto decode_adaptive_block(std::span<std::byte const> payload,
  std::size_t count,
  std::size_t limit) -> std::vector<std::byte>
{
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "Adaptive range output exceeds limit");
  }
  Model probabilities;
  range_decoder decoder(payload);
  std::vector<std::byte> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto const symbol = probabilities.find_symbol(decoder.cumulative(probabilities.total()));
    auto const interval = probabilities.interval_for(symbol);
    decoder.decode(interval.cumulative_, interval.frequency_, interval.total_);
    result.push_back(static_cast<std::byte>(symbol));
    probabilities.update(symbol);
  }
  decoder.finish();
  return result;
}
}// namespace dacoexec::detail::arithmetic
#endif
