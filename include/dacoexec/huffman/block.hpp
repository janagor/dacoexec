//! \file
//! Owned block compression, RAW fallback, and checked decompression.

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
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec::huffman {

/**
 * Payload representation: original bytes or canonical Huffman bits.
 */
enum class block_encoding : std::uint8_t { raw, canonical_huffman };

/**
 * Move-only owning block model, independent of any serialized format.
 * RAW has zero lengths/padding and payload size equal to uncompressed_size_.
 * Huffman represents nonempty input, with canonical lengths and zero-filled
 * low padding bits (fewer than one byte). sequence_ survives both operations.
 */
struct encoded_block
{
  //! Source block position, preserved across compression and decompression.
  std::uint64_t sequence_ = 0;
  //! Selects payload interpretation.
  block_encoding encoding_ = block_encoding::raw;
  //! Exact decoded byte count.
  std::uint64_t uncompressed_size_ = 0;
  //! Owned RAW bytes or MSB-first packed Huffman data.
  std::vector<std::byte> payload_;
  //! Per-symbol canonical widths; all zero for RAW.
  code_length_table lengths_{};
  //! Unused zero low bits in the last Huffman byte; zero for RAW.
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
    std::uint64_t total = 0;
    for (auto const symbol : std::views::iota(std::size_t{ 0 }, alphabet_size)) {
      auto const frequency = frequencies.at(symbol);
      auto const length = lengths.at(symbol);
      if (length > max_canonical_bits) { return std::nullopt; }
      if (length == 0) { continue; }

      auto const remaining_bits = std::numeric_limits<std::uint64_t>::max() - total;
      if (frequency > remaining_bits / length) { return std::nullopt; }
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
    constexpr auto byte_bits = dacoexec::detail::bits_per_byte;
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
    for (auto const symbol :
      block.payload_ | views::decode(book, valid_bits, block.uncompressed_size_, infer_padding)) {
      output.push_back(symbol);
    }
    return output;
  }
}// namespace detail

/**
 * Consumes one block. Choose Huffman only when packed bytes plus the complete
 * length table are strictly smaller than the original. RAW moves the buffer.
 * Empty input, ties, widths above max_canonical_bits, and unrepresentable
 * encoded bit totals select RAW. The cost includes the 256-byte length table,
 * but no external framing. Huffman payloads have zero-filled low padding bits.
 *
 * @param block Owned bytes to consume; sequence_ is preserved.
 * @return Independent owned encoded data.
 * @throws std::overflow_error If frequency counting or tree sums overflow.
 * Allocation failures propagate.
 * @code
 * auto encoded = huffman::compress_block(std::move(block));
 * auto decoded = huffman::decompress_block(std::move(encoded), output_limit);
 * @endcode
 *
 * @see decompress_block
 */
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
      for (auto const word : block.bytes_ | views::codewords(book)) {
        writer.write_bits({ .value_ = word.bits_, .bit_count_ = word.length_ });
      }
      result.padding_bits_ = writer.finish();
      result.lengths_ = lengths;
      result.encoding_ = block_encoding::canonical_huffman;
      return result;
    }
  }
  result.payload_ = std::move(block.bytes_);
  return result;
}

/**
 * Malformed content throws decode_error; allocation failures propagate.
 * The output bound is checked before reconstructing or allocating output.
 * infer_padding is for framing formats without a padding-count field: supply
 * padding_bits_ == 0 and accept only zero padding in the final partial byte.
 * Explicit mode subtracts padding_bits_ from payload bits and requires an exact
 * meaningful-bit count. Inferred mode derives the fewer-than-eight zero bits
 * left after uncompressed_size_ symbols. Both modes reject trailing bytes.
 * RAW metadata requires equal payload/output sizes and zero lengths/padding.
 *
 * @param block Owned encoded data consumed by this operation.
 * @param max_output_bytes Maximum decoded bytes, checked before output allocation.
 * @param infer_padding Infer final padding instead of using an explicit count.
 * @return Independently owned bytes with the original sequence number; RAW
 *     transfers the payload directly. No partial output is returned on failure.
 * @throws decode_error If metadata, code lengths, payload, padding, or bounds
 *     are invalid.
 * @see compress_block
 */
[[nodiscard]] inline auto decompress_block(encoded_block block,
  std::size_t max_output_bytes,
  bool infer_padding = false) -> dacoexec::byte_block
{
  detail::validate_block(block, max_output_bytes);
  if (infer_padding && block.padding_bits_ != 0) {
    throw decode_error("Inferred padding requires a zero padding count");
  }
  if (block.encoding_ == block_encoding::raw) { return { block.sequence_, std::move(block.payload_) }; }
  auto const book = detail::make_decode_codebook(block.lengths_);
  auto const valid_bits = detail::meaningful_bit_count(block);
  return { block.sequence_, detail::decode_payload(block, book, valid_bits, infer_padding) };
}

}// namespace dacoexec::huffman

#endif// DACOEXEC_HUFFMAN_BLOCK_HPP
