//! \file
//! Owned block compression, RAW fallback, and checked decompression.
#ifndef DACOEXEC_HUFFMAN_BLOCK_HPP
#define DACOEXEC_HUFFMAN_BLOCK_HPP

#include <dacoexec/detail/huffman/decoder.hpp>
#include <dacoexec/detail/huffman/encoder.hpp>
#include <utility>

namespace dacoexec::detail::huffman {

/**
 * Consumes one block. Choose Huffman only when packed bytes plus the complete
 * length table are strictly smaller than the original. RAW moves the buffer.
 * Empty input, ties, widths above max_canonical_bits, and unrepresentable
 * encoded bit totals select RAW. The cost includes the 256-byte length table,
 * but no external framing. Huffman payloads have zero-filled low padding bits.
 *
 * @param block Owned bytes to consume; sequence_ is preserved.
 * @return Independent owned encoded data.
 * @throws dacoexec::exception (errc::numeric_overflow) If frequency counting or tree sums overflow.
 * Allocation failures propagate.
 * @code
 * auto encoded = huffman::compress_block(std::move(block));
 * auto decoded = huffman::decompress_block(std::move(encoded), output_limit);
 * @endcode
 *
 * @see decompress_block
 */
[[nodiscard]] inline auto compress_block(dacoexec::detail::byte_block block) -> encoded_block
{ return encoder{}.encode(std::move(block)); }

/**
 * Malformed content throws dacoexec::exception (errc::invalid_payload); allocation failures propagate.
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
 * @throws dacoexec::exception (errc::invalid_payload) If metadata, code lengths, payload, or padding are invalid.
 * @throws dacoexec::exception (errc::output_limit_exceeded) If the decoded size exceeds max_output_bytes.
 * @see compress_block
 */
[[nodiscard]] inline auto decompress_block(encoded_block block,
  std::size_t max_output_bytes,
  bool infer_padding = false) -> dacoexec::detail::byte_block
{ return decoder{}.decode(std::move(block), max_output_bytes, infer_padding); }

}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_BLOCK_HPP
