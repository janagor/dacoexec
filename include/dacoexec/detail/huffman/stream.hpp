//! \file
//! Incremental DHF1 compression and verified decompression.

#ifndef DACOEXEC_HUFFMAN_STREAM_HPP
#define DACOEXEC_HUFFMAN_STREAM_HPP

#include <dacoexec/detail/codec/stream_statistics.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/detail/views/blocks.hpp>
#include <dacoexec/detail/views/frames.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::huffman {
/**
 * Settings for incremental compression.
 *
 * @see compress_stream
 */
struct stream_options
{
  //! Maximum source bytes per data frame; must be in [1, max_frame_size].
  std::size_t block_size_ = default_block_size;
};

/**
 * Resource limits for verified stream decompression.
 *
 * @see decompress_stream
 */
struct decode_options
{
  //! Limit on each payload and uncompressed size; must be in [1, max_frame_size].
  std::size_t max_frame_size_ = huffman::max_frame_size;
  //! Maximum total decoded bytes; zero permits only an archive without data frames.
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
};

using codec::stream_statistics;
using codec::checked_add;

/**
 * Consumes one nonempty block, computes its uncompressed CRC32 and encodes it.
 *
 * Encoding, frame ownership and serialization are independent of scheduling.
 * The result owns its bytes and retains the source sequence; nothing is written.
 * RAW versus Huffman follows compress_block(), including its length-table cost.
 *
 * @param block Owned input of at most max_frame_size bytes.
 * @return Consistent header and owned encoded body ready for write_frame().
 * @throws format_error If input is empty or exceeds the supported frame size.
 * Codec and allocation exceptions propagate.
 * @see compress_block
 * @see write_frame
 */
[[nodiscard]] inline auto encode_frame(byte_block block) -> encoded_frame
{
  if (block.bytes_.empty() || block.bytes_.size() > max_frame_size) { throw format_error("Invalid input block size"); }
  auto const checksum = dacoexec::detail::support::crc32(block.bytes_);
  encoded_frame frame;
  frame.block_ = compress_block(std::move(block));
  frame.header_ = { .kind_ = frame.block_.encoding_ == block_encoding::raw ? frame_kind::raw : frame_kind::huffman,
    .uncompressed_size_ = static_cast<std::uint32_t>(frame.block_.uncompressed_size_),
    .payload_size_ = static_cast<std::uint32_t>(frame.block_.payload_.size()),
    .crc32_ = checksum };
  return frame;
}

/**
 * Compresses a viewable byte input range into a DHF1 archive in one pass.
 *
 * Partitions input into independently owned blocks of at most options.block_size_
 * bytes, choosing RAW or canonical Huffman for each. Writes the archive header
 * before consuming blocks and END after all data. Empty input writes no data
 * frames. Memory is staged per block rather than buffering the complete archive.
 * Output is incremental and non-transactional: accepted bytes, including earlier
 * frames, remain in the sink if source, encoder, allocation, or writing fails.
 * Those exceptions propagate without retry.
 *
 * @param source Input bytes, including unsized and single-pass ranges.
 * @param sink Destination satisfying io::byte_output_sink.
 * @param options Compression block size.
 * @return Completed statistics including archive framing and length tables.
 * @throws std::invalid_argument If block_size_ is zero or exceeds max_frame_size,
 *     before the archive header is written.
 * @throws std::overflow_error If codec counters or statistics overflow.
 *
 * @code
 * io::iterator_sink sink{std::back_inserter(archive)};
 * auto stats = huffman::compress_stream(bytes, sink);
 * @endcode
 *
 * @see decompress_stream
 * @see stream_statistics
 */
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress_stream(R &&source, S &sink, stream_options options = {}) -> stream_statistics
{
  if (options.block_size_ == 0 || options.block_size_ > max_frame_size) {
    throw std::invalid_argument("Invalid block size");
  }
  stream_statistics statistics{ .output_bytes_ = archive_header_size + frame_header_size };
  write_archive_header(sink);
  for (auto block : std::forward<R>(source) | dacoexec::detail::views::blocks(options.block_size_)) {
    auto frame = encode_frame(std::move(block));
    ::dacoexec::detail::huffman::checked_add(statistics.input_bytes_, frame.header_.uncompressed_size_);
    ::dacoexec::detail::huffman::checked_add(statistics.output_bytes_,
      frame_header_size + frame.block_.payload_.size()
        + (frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U));
    ::dacoexec::detail::huffman::checked_add(statistics.frames_, 1);
    write_frame(sink, frame);
  }
  write_frame_header(sink, {});
  return statistics;
}

/**
 * Earlier frames remain written if a later frame fails. Current frame content
 * and output limits are validated before its first write; sinks cannot roll back.
 *
 * Consumes a viewable byte input range once, parsing one complete owned frame
 * at a time. Decodes Huffman with inferred zero padding and verifies IEEE CRC32
 * over decoded bytes before calling sink.write() for that frame. RAW frames
 * also receive checksum validation. An empty archive writes no bytes.
 * END must be present with no trailing input. A later malformed frame or END
 * leaves earlier output intact; a sink failure can leave a prefix of the
 * current verified frame. Source, allocation, and sink exceptions propagate.
 *
 * @param source Complete DHF1 archive, including header and END.
 * @param sink Destination for decoded bytes.
 * @param options Per-frame size and cumulative decoded-output limits.
 * @return Completed statistics; input includes framing and output is plain bytes.
 * @throws std::invalid_argument If max_frame_size_ is zero or exceeds the format limit.
 * @throws format_error For malformed framing or code-length tables, checksum
 *     mismatch, or cumulative output exceeding max_output_bytes_.
 * @throws decode_error For malformed block content or padding.
 * @throws std::overflow_error If statistics or frame sequence numbers overflow.
 * @see compress_stream
 * @see decode_options
 */
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress_stream(R &&source, S &sink, decode_options options = {}) -> stream_statistics
{
  stream_statistics statistics{ .input_bytes_ = archive_header_size + frame_header_size };
  for (auto frame : std::forward<R>(source) | dacoexec::detail::views::frames(options.max_frame_size_)) {
    if (frame.header_.uncompressed_size_ > options.max_output_bytes_ - statistics.output_bytes_) {
      throw format_error("Decompressed output exceeds configured limit");
    }
    ::dacoexec::detail::huffman::checked_add(statistics.input_bytes_,
      frame_header_size + frame.block_.payload_.size()
        + (frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U));
    ::dacoexec::detail::huffman::checked_add(statistics.output_bytes_, frame.header_.uncompressed_size_);
    ::dacoexec::detail::huffman::checked_add(statistics.frames_, 1);
    auto decoded = decompress_block(std::move(frame.block_), options.max_frame_size_, true);
    if (dacoexec::detail::support::crc32(decoded.bytes_) != frame.header_.crc32_) {
      throw format_error("DHF1 checksum mismatch");
    }
    sink.write(decoded.bytes_);
  }
  return statistics;
}
}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_STREAM_HPP
