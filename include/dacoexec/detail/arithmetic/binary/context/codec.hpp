//! Context-adaptive binary adapter over the shared E1/E2/E3 engine.
#ifndef DACOEXEC_ARITHMETIC_BINARY_CONTEXT_CODEC_HPP
#define DACOEXEC_ARITHMETIC_BINARY_CONTEXT_CODEC_HPP

#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

#include <dacoexec/detail/arithmetic/binary/codec.hpp>
#include <dacoexec/detail/arithmetic/binary/context/model.hpp>

namespace dacoexec::detail::arithmetic {
template<byte_input_range R> [[nodiscard]] auto encode_context_binary_block(R &&source) -> std::vector<std::byte>
{
  binary_arithmetic_encoder encoder;
  context_model model;
  for (auto &&value : source) {
    using source_value = std::ranges::range_value_t<R>;
    auto const byte = support::to_byte(static_cast<source_value>(value));
    for (unsigned remaining = support::bits_per_byte; remaining != 0; --remaining) {
      auto const bit = (std::to_integer<unsigned>(byte) & (1U << (remaining - 1))) != 0;
      encoder.encode(bit, model.probabilities());
      model.update(bit);
    }
  }
  return encoder.finish();
}

[[nodiscard]] inline auto decode_context_binary_block(std::span<std::byte const> payload,
  std::size_t count,
  std::size_t limit) -> std::vector<std::byte>
{
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "Context binary output exceeds limit");
  }
  binary_arithmetic_decoder decoder(payload);
  context_model model;
  std::vector<std::byte> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    unsigned byte = 0;
    for (unsigned position = 0; position < support::bits_per_byte; ++position) {
      auto const bit = decoder.decode(model.probabilities());
      model.update(bit);
      byte = (byte << 1U) | static_cast<unsigned>(bit);
    }
    result.push_back(static_cast<std::byte>(byte));
  }
  decoder.finish();
  return result;
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_CONTEXT_CODEC_HPP
