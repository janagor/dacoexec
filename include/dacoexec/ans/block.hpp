//! \file
//! Owned ANS blocks with metadata-aware RAW fallback.
#ifndef DACOEXEC_ANS_BLOCK_HPP
#define DACOEXEC_ANS_BLOCK_HPP
#include <dacoexec/ans/rans.hpp>
#include <dacoexec/views/blocks.hpp>
#include <algorithm>
#include <optional>
#include <utility>
namespace dacoexec::ans {
inline constexpr std::size_t model_prefix_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t model_entry_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t min_model_wire_size = model_prefix_size + model_entry_size;

[[nodiscard]] inline auto model_wire_size(frequency_model<alphabet_size> const &model) -> std::size_t
{
  auto const present = std::ranges::count_if(
    model.frequencies(), [](auto frequency) -> bool { return frequency != 0; });
  return model_prefix_size + (static_cast<std::size_t>(present) * model_entry_size);
}
enum class block_encoding : std::uint8_t { raw, rans };
struct encoded_block
{
  std::uint64_t sequence_ = 0;
  block_encoding encoding_ = block_encoding::raw;
  std::uint64_t uncompressed_size_ = 0;
  std::vector<std::byte> payload_;
  std::optional<frequency_model<alphabet_size>> model_;
};
namespace detail {
[[nodiscard]] inline auto model_metadata_size(encoded_block const &block) -> std::size_t
{
  if (block.encoding_ == block_encoding::raw) { return 0; }
  if (!block.model_) { throw decode_error("Missing ANS model"); }
  return model_wire_size(*block.model_);
}
}// namespace detail

[[nodiscard]] inline auto compress_block(byte_block block) -> encoded_block
{
  encoded_block result;
  result.sequence_ = block.sequence_;
  result.uncompressed_size_ = block.bytes_.size();
  if (block.bytes_.size() > min_model_wire_size) {
    auto model = make_model(count_frequencies(block.bytes_));
    auto payload = rans::encode_block(block.bytes_, model);
    auto const metadata_size = model_wire_size(model);
    if (metadata_size < block.bytes_.size() && payload.size() < block.bytes_.size() - metadata_size) {
      result.encoding_ = block_encoding::rans;
      result.payload_ = std::move(payload);
      result.model_ = model;
      return result;
    }
  }
  result.payload_ = std::move(block.bytes_);
  return result;
}
[[nodiscard]] inline auto decompress_block(encoded_block block, std::size_t limit) -> byte_block
{
  if (block.uncompressed_size_ > limit) { throw decode_error("ANS output exceeds configured limit"); }
  if (block.encoding_ == block_encoding::raw) {
    if (block.model_ || block.payload_.size() != block.uncompressed_size_) { throw decode_error("Invalid ANS RAW metadata"); }
    return { block.sequence_, std::move(block.payload_) };
  }
  if (block.encoding_ != block_encoding::rans || !block.model_) { throw decode_error("Invalid ANS block metadata"); }
  return { block.sequence_, rans::decode_block(block.payload_, *block.model_,
    static_cast<std::size_t>(block.uncompressed_size_), limit) };
}
}// namespace dacoexec::ans
#endif// DACOEXEC_ANS_BLOCK_HPP
