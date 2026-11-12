//! Fixed binary model adapter over the E1/E2/E3 arithmetic engine.
#ifndef DACOEXEC_ARITHMETIC_BINARY_ARITHMETIC_CODEC_HPP
#define DACOEXEC_ARITHMETIC_BINARY_ARITHMETIC_CODEC_HPP

#include <array>
#include <dacoexec/detail/arithmetic/binary/decoder.hpp>
#include <dacoexec/detail/arithmetic/binary/encoder.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <ranges>
#include <span>
#include <vector>

namespace dacoexec::detail::arithmetic {
inline constexpr std::size_t binary_model_metadata_size = 4;
inline constexpr std::size_t binary_min_payload_size = binary_state_bits / support::bits_per_byte;

//! Two LE uint16 frequency-minus-one values; both positive, sum <= 65536.
[[nodiscard]] inline auto serialize_binary_model(binary_model const &probabilities)
  -> std::array<std::byte, binary_model_metadata_size>
{
  static_cast<void>(probabilities.total());
  auto const zero = support::write_u16_le(static_cast<std::uint16_t>(probabilities.zero_frequency_ - 1));
  auto const one = support::write_u16_le(static_cast<std::uint16_t>(probabilities.one_frequency_ - 1));
  return { zero[0], zero[1], one[0], one[1] };
}

[[nodiscard]] inline auto deserialize_binary_model(std::span<std::byte const, binary_model_metadata_size> metadata)
  -> binary_model
{
  binary_model probabilities{ std::uint32_t{ support::read_u16_le(metadata.first<2>()) } + 1,
    std::uint32_t{ support::read_u16_le(metadata.last<2>()) } + 1 };
  if (probabilities.zero_frequency_ + probabilities.one_frequency_ > binary_max_total) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Invalid binary model metadata");
  }
  return probabilities;
}

template<byte_input_range R>
[[nodiscard]] auto encode_binary_block(R &&source, binary_model const &probabilities) -> std::vector<std::byte>
{
  static_cast<void>(probabilities.total());
  binary_arithmetic_encoder encoder;
  for (auto &&value : source) {
    using source_value = std::ranges::range_value_t<R>;
    encoder.encode_byte(support::to_byte(static_cast<source_value>(value)), probabilities);
  }
  return encoder.finish();
}

[[nodiscard]] inline auto decode_binary_block(std::span<std::byte const> payload,
  binary_model const &probabilities,
  std::size_t count,
  std::size_t limit) -> std::vector<std::byte>
{
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "Binary arithmetic output exceeds limit");
  }
  static_cast<void>(probabilities.total());
  binary_arithmetic_decoder decoder(payload);
  std::vector<std::byte> result;
  result.reserve(count);
  // Decode eight MSB-first bits per byte without multiplying an untrusted count.
  for (std::size_t index = 0; index < count; ++index) { result.push_back(decoder.decode_byte(probabilities)); }
  decoder.finish();
  return result;
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BINARY_ARITHMETIC_CODEC_HPP
