//! \file
//! Incremental DHF1 compression and verified decompression.

#ifndef DACOEXEC_HUFFMAN_STREAM_HPP
#define DACOEXEC_HUFFMAN_STREAM_HPP

#include <dacoexec/detail/codec/stream.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/detail/views/frames.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
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

struct stream_format
{
  using frame_type = encoded_frame;
  using options_type = stream_options;
  using decode_options_type = decode_options;
  static constexpr auto archive_header_size = huffman::archive_header_size;
  static constexpr auto frame_header_size = huffman::frame_header_size;
  static constexpr auto max_frame_size = huffman::max_frame_size;

  template<class Options> static auto validate_options(Options const & /*options*/) -> void {}

  [[nodiscard]] static auto metadata_size(encoded_frame const &frame) -> std::size_t
  { return frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U; }

  template<io::byte_output_sink Sink> static auto write_archive_header(Sink &sink) -> void
  { huffman::write_archive_header(sink); }

  template<io::byte_output_sink Sink> static auto write_frame(Sink &sink, encoded_frame const &frame) -> void
  { huffman::write_frame(sink, frame); }

  template<io::byte_output_sink Sink> static auto write_end(Sink &sink) -> void
  { huffman::write_frame_header(sink, {}); }

  [[nodiscard]] static auto encode_frame(byte_block block, stream_options const & /*options*/) -> encoded_frame
  { return huffman::encode_frame(std::move(block)); }

  template<std::ranges::viewable_range R> [[nodiscard]] static auto frames(R &&source, std::size_t limit)
  { return std::forward<R>(source) | ::dacoexec::detail::views::frames(limit); }

  [[nodiscard]] static auto decode_block(encoded_block block, std::size_t limit) -> byte_block
  { return huffman::decompress_block(std::move(block), limit, true); }

  [[nodiscard]] static auto checksum_message() noexcept -> char const * { return "DHF1 checksum mismatch"; }
};

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
{ return codec::compress_stream<stream_format>(std::forward<R>(source), sink, options); }

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
{ return codec::decompress_stream<stream_format>(std::forward<R>(source), sink, options); }
}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_STREAM_HPP
