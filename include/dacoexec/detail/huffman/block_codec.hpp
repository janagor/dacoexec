//! \file
#ifndef DACOEXEC_HUFFMAN_DETAIL_BLOCK_CODEC_HPP
#define DACOEXEC_HUFFMAN_DETAIL_BLOCK_CODEC_HPP

#include <algorithm>
#include <dacoexec/detail/huffman/decode_view.hpp>
#include <dacoexec/detail/huffman/encoded_block.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <iterator>
#include <limits>
#include <vector>

namespace dacoexec::detail::huffman {

inline auto validate_block(encoded_block const &block) -> void
{
  if (block.encoding_ == block_encoding::raw) {
    if (block.padding_bits_ != 0
        || std::ranges::any_of(block.lengths_, [](auto length) -> bool { return length != 0; })) {
      throw decode_error("Inconsistent RAW block metadata");
    }
  } else if (block.encoding_ == block_encoding::canonical_huffman) {
    if (block.uncompressed_size_ == 0 || block.payload_.empty()
        || block.padding_bits_ >= dacoexec::detail::support::bits_per_byte) {
      throw decode_error("Inconsistent Huffman block metadata");
    }
  } else {
    throw decode_error("Unsupported block encoding");
  }
}

[[nodiscard]] inline auto make_decode_codebook(code_length_table const &lengths) -> canonical_codebook
{
  try {
    return make_canonical_codebook(lengths);
  } catch (std::invalid_argument const &error) {
    throw decode_error(error.what());
  } catch (std::length_error const &error) {
    throw decode_error(error.what());
  }
}

[[nodiscard]] inline auto meaningful_bit_count(encoded_block const &block) -> std::uint64_t
{
  constexpr auto byte_bits = dacoexec::detail::support::bits_per_byte;
  if (block.payload_.size() > std::numeric_limits<std::uint64_t>::max() / byte_bits) {
    throw decode_error("Huffman payload bit count overflow");
  }
  std::uint64_t const payload_bytes = block.payload_.size();
  auto const valid_bits = (payload_bytes * byte_bits) - block.padding_bits_;
  // Every symbol consumes at least one meaningful bit. Reject impossible
  // declarations before reserving output even when the caller allows them.
  if (block.uncompressed_size_ > valid_bits) {
    throw decode_error("Huffman payload cannot produce declared output size");
  }
  return valid_bits;
}

[[nodiscard]] inline auto decode_payload(encoded_block const &block,
  canonical_codebook const &book,
  std::uint64_t valid_bits,
  bool infer_padding) -> std::vector<std::byte>
{
  std::vector<std::byte> output;
  if (block.uncompressed_size_ > output.max_size()) { throw decode_error("Huffman output exceeds vector capacity"); }
  output.reserve(static_cast<std::size_t>(block.uncompressed_size_));
  std::ranges::copy(block.payload_ | views::decode(book, valid_bits, block.uncompressed_size_, infer_padding),
    std::back_inserter(output));
  return output;
}

}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_DETAIL_BLOCK_CODEC_HPP
