//! Payload: uint32 LE state, uint32 LE valid bit count, then MSB-first bits.
//! Reverse encoding appends each transition's bits least-significant first;
//! reversing the complete bit sequence gives forward decoding order. Padding is zero.
#ifndef DACOEXEC_ANS_TANS_CODEC_HPP
#define DACOEXEC_ANS_TANS_CODEC_HPP
#include <cassert>
#include <dacoexec/detail/ans/tans/table.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>
#include <limits>
#include <ranges>
#include <span>

namespace dacoexec::detail::ans::tans {
inline constexpr unsigned packed_byte_bits = 8;
inline constexpr unsigned packed_byte_mask = 0xFF;
inline constexpr std::size_t payload_prefix_size = 2 * sizeof(std::uint32_t);

// The serialized payload bit count is uint32_t, independently of output limits.
[[nodiscard]] constexpr auto checked_payload_bit_count(std::uint32_t count, unsigned added) -> std::uint32_t
{
  if (added > std::numeric_limits<std::uint32_t>::max() - count) {
    throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "tANS payload bit count overflow");
  }
  return count + added;
}

[[nodiscard]] inline auto encode_block(std::span<std::byte const> input, tables const &table) -> std::vector<std::byte>
{
  std::vector<std::byte> reverse;
  std::uint32_t count = 0;
  std::uint32_t state = 0;
  std::uint64_t accumulator = 0;
  unsigned buffered = 0;
  for (auto symbol : std::views::reverse(input)) {
    auto const transition = table.encode(state, std::to_integer<unsigned>(symbol));
    count = checked_payload_bit_count(count, transition.num_bits_);
    accumulator |= std::uint64_t{ transition.bits_ } << buffered;
    buffered += transition.num_bits_;
    while (buffered >= packed_byte_bits) {
      reverse.push_back(static_cast<std::byte>(accumulator & packed_byte_mask));
      accumulator >>= packed_byte_bits;
      buffered -= packed_byte_bits;
    }
    state = transition.state_;
  }
  if (buffered != 0) { reverse.push_back(static_cast<std::byte>(accumulator)); }
  std::vector<std::byte> result;
  for (auto value : { state, count }) {
    auto bytes = support::write_u32_le(value);
    result.insert(result.end(), bytes.begin(), bytes.end());
  }
  result.resize(payload_prefix_size + reverse.size());
  // Reverse byte order and align the partial leading byte. Each output byte
  // combines at most two stack bytes; transition bits never need individual reversal.
  auto const padding = (packed_byte_bits - (count % packed_byte_bits)) % packed_byte_bits;
  for (std::size_t byte = 0; byte < reverse.size(); ++byte) {
    auto const source = reverse.size() - byte - 1;
    auto value = std::to_integer<unsigned>(reverse.at(source)) << padding;
    if (padding != 0 && source != 0) {
      value |= std::to_integer<unsigned>(reverse.at(source - 1)) >> (packed_byte_bits - padding);
    }
    result.at(payload_prefix_size + byte) = static_cast<std::byte>(value & packed_byte_mask);
  }
  return result;
}

[[nodiscard]] inline auto encode_block(std::span<std::byte const> input, frequency_model<alphabet_size> const &model)
  -> std::vector<std::byte>
{ return encode_block(input, tables{ model }); }

[[nodiscard]] inline auto decode_block(std::span<std::byte const> payload,
  tables const &table,
  std::size_t count,
  std::size_t limit = std::numeric_limits<std::size_t>::max()) -> std::vector<std::byte>
{
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "tANS output exceeds configured limit");
  }
  if (payload.size() < payload_prefix_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid tANS payload size");
  }
  auto state = support::read_u32_le(payload.first<4>());
  auto const bits = support::read_u32_le(payload.subspan<4, 4>());
  if (state >= table.total()
      || payload.size() - payload_prefix_size
           != (static_cast<std::size_t>(bits) + (packed_byte_bits - 1U)) / packed_byte_bits) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid tANS state or bit length");
  }
  if (bits % packed_byte_bits != 0
      && (std::to_integer<unsigned>(payload.back()) & ((1U << (packed_byte_bits - (bits % packed_byte_bits))) - 1U))
           != 0) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid tANS padding");
  }
  std::vector<std::byte> output;
  output.reserve(count);
  std::uint32_t position = 0;
  std::uint64_t accumulator = 0;
  unsigned buffered = 0;
  auto const packed = payload.subspan(payload_prefix_size);
  auto cursor = packed.begin();
  // At every transition boundary, bytes_read * packed_byte_bits == position +
  // buffered. The metadata checks bound position by bits and packed.size() by
  // ceil(bits / packed_byte_bits). After checking this transition's bit demand,
  // buffered < num_bits therefore implies that another payload byte exists.
  // The assertion makes that invariant executable in debug/sanitizer builds.
  for (std::size_t symbol = 0; symbol < count; ++symbol) {
    auto const &entry = table.decode(state);
    if (entry.num_bits_ > bits - position) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Truncated tANS bits");
    }
    while (buffered < entry.num_bits_) {
      assert(cursor != packed.end());
      accumulator = (accumulator << packed_byte_bits) | std::to_integer<unsigned>(*cursor++);
      buffered += packed_byte_bits;
    }
    buffered -= entry.num_bits_;
    auto const value = static_cast<std::uint32_t>((accumulator >> buffered) & ((1U << entry.num_bits_) - 1U));
    accumulator &= (std::uint64_t{ 1 } << buffered) - 1U;
    position += entry.num_bits_;
    output.push_back(static_cast<std::byte>(entry.symbol_));
    state = entry.baseline_ + value;
  }
  if (state != 0 || position != bits) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid tANS final state or excess bits");
  }
  return output;
}

[[nodiscard]] inline auto decode_block(std::span<std::byte const> payload,
  frequency_model<alphabet_size> const &model,
  std::size_t count,
  std::size_t limit = std::numeric_limits<std::size_t>::max()) -> std::vector<std::byte>
{
  if (count > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "tANS output exceeds configured limit");
  }
  if (payload.size() < payload_prefix_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid tANS payload size");
  }
  if (model.precision_bits() < min_table_log || model.precision_bits() > max_table_log) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid tANS table log");
  }
  return decode_block(payload, tables{ model }, count, limit);
}

}// namespace dacoexec::detail::ans::tans
#endif
