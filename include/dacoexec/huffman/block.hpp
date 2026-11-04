#ifndef DACOEXEC_HUFFMAN_BLOCK_HPP
#define DACOEXEC_HUFFMAN_BLOCK_HPP

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/huffman/codewords_view.hpp>
#include <dacoexec/huffman/decode_view.hpp>
#include <dacoexec/views/blocks.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec::huffman {

enum class block_encoding : std::uint8_t { raw, canonical_huffman };

// Internal owning block model, independent of any serialized format.
// RAW has zero lengths/padding and payload size equal to uncompressed_size_.
// Huffman represents nonempty input, with canonical lengths and zero-filled
// low padding bits (fewer than one byte). sequence_ survives both operations.
struct encoded_block
{
  std::uint64_t sequence_ = 0;
  block_encoding encoding_ = block_encoding::raw;
  std::uint64_t uncompressed_size_ = 0;
  std::vector<std::byte> payload_;
  code_length_table lengths_{};
  std::uint8_t padding_bits_ = 0;

  encoded_block() = default;
  encoded_block(encoded_block &&) noexcept = default;
  auto operator=(encoded_block &&) noexcept -> encoded_block & = default;
  encoded_block(encoded_block const &) = delete;
  auto operator=(encoded_block const &) -> encoded_block & = delete;
  ~encoded_block() = default;
};

namespace detail {
  // Unsupported widths and unrepresentable bit totals select RAW before
  // allocating an encoded buffer. Lengths come from build_code_lengths;
  // canonical validation and assignment happen once, only if Huffman wins.
  [[nodiscard]] inline auto encoded_bit_count(frequency_table const &frequencies, code_length_table const &lengths)
    -> std::optional<std::uint64_t>
  {
    if (std::ranges::any_of(lengths, [](auto length) -> bool { return length > max_canonical_bits; })) {
      return std::nullopt;
    }
    std::uint64_t total = 0;
    for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
      auto const frequency = frequencies.at(symbol);
      auto const length = lengths.at(symbol);
      if (length != 0 && frequency > (std::numeric_limits<std::uint64_t>::max() - total) / length) {
        return std::nullopt;
      }
      total += frequency * length;
    }
    return total;
  }

  inline auto validate_block(encoded_block const &block, std::size_t max_output_bytes) -> void
  {
    if (block.uncompressed_size_ > max_output_bytes) { throw decode_error("Huffman output exceeds configured limit"); }
    if (block.encoding_ == block_encoding::raw) {
      if (block.payload_.size() != block.uncompressed_size_ || block.padding_bits_ != 0
          || std::ranges::any_of(block.lengths_, [](auto length) -> bool { return length != 0; })) {
        throw decode_error("Inconsistent RAW block metadata");
      }
    } else if (block.encoding_ == block_encoding::canonical_huffman) {
      if (block.uncompressed_size_ == 0 || block.payload_.empty()
          || block.padding_bits_ >= dacoexec::detail::bits_per_byte) {
        throw decode_error("Inconsistent Huffman block metadata");
      }
    } else {
      throw decode_error("Unsupported block encoding");
    }
  }
}// namespace detail

// Consumes one block. Choose Huffman only when packed bytes plus the complete
// length table are strictly smaller than the original. RAW moves the buffer.
[[nodiscard]] inline auto compress_block(dacoexec::byte_block block) -> encoded_block
{
  encoded_block result;
  result.sequence_ = block.sequence_;
  result.uncompressed_size_ = block.bytes_.size();
  auto const frequencies = count_frequencies(block.bytes_);
  auto const lengths = build_code_lengths(frequencies);
  auto const bits = detail::encoded_bit_count(frequencies, lengths);
  if (bits && *bits != 0) {
    constexpr auto byte_bits = dacoexec::detail::bits_per_byte;
    auto const packed_size = (*bits / byte_bits) + (*bits % byte_bits != 0 ? 1U : 0U);
    // Subtraction avoids overflowing the sum of payload and metadata costs.
    if (block.bytes_.size() > alphabet_size && packed_size < block.bytes_.size() - alphabet_size) {
      auto const book = make_canonical_codebook(lengths);
      result.payload_.reserve(static_cast<std::size_t>(packed_size));
      dacoexec::bit_writer writer{ std::back_inserter(result.payload_) };
      for (auto const word : block.bytes_ | views::codewords(book)) { writer.write_bits(word.bits_, word.length_); }
      result.padding_bits_ = writer.finish();
      result.lengths_ = lengths;
      result.encoding_ = block_encoding::canonical_huffman;
      return result;
    }
  }
  result.payload_ = std::move(block.bytes_);
  return result;
}

// Malformed content throws decode_error; allocation failures propagate.
// The output bound is checked before reconstructing or allocating output.
// infer_padding is for framing formats without a padding-count field: supply
// padding_bits_ == 0 and accept only zero padding in the final partial byte.
[[nodiscard]] inline auto decompress_block(encoded_block block, std::size_t max_output_bytes, bool infer_padding = false) -> dacoexec::byte_block
{
  detail::validate_block(block, max_output_bytes);
  if (infer_padding && block.padding_bits_ != 0) { throw decode_error("Inferred padding requires a zero padding count"); }
  if (block.encoding_ == block_encoding::raw) { return { block.sequence_, std::move(block.payload_) }; }
  auto const book = [&block]() -> canonical_codebook {
    try {
      return make_canonical_codebook(block.lengths_);
    } catch (std::invalid_argument const &error) {
      throw decode_error(error.what());
    } catch (std::length_error const &error) {
      throw decode_error(error.what());
    }
  }();
  constexpr auto byte_bits = dacoexec::detail::bits_per_byte;
  if (block.payload_.size() > std::numeric_limits<std::uint64_t>::max() / byte_bits) {
    throw decode_error("Huffman payload bit count overflow");
  }
  auto const valid_bits = (static_cast<std::uint64_t>(block.payload_.size()) * byte_bits) - block.padding_bits_;
  // Every symbol consumes at least one meaningful bit. Reject impossible
  // declarations before reserving output even when the caller allows them.
  if (block.uncompressed_size_ > valid_bits) {
    throw decode_error("Huffman payload cannot produce declared output size");
  }
  std::vector<std::byte> output;
  if (block.uncompressed_size_ > output.max_size()) { throw decode_error("Huffman output exceeds vector capacity"); }
  output.reserve(static_cast<std::size_t>(block.uncompressed_size_));
  for (auto const symbol : block.payload_ | views::decode(book, valid_bits, block.uncompressed_size_, infer_padding)) {
    output.push_back(symbol);
  }
  return { block.sequence_, std::move(output) };
}

}// namespace dacoexec::huffman

#endif // DACOEXEC_HUFFMAN_BLOCK_HPP
