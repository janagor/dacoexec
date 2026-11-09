//! Incremental archive compression and decompression.
#ifndef DACOEXEC_HUFFMAN_STREAM_HPP
#define DACOEXEC_HUFFMAN_STREAM_HPP
#include <dacoexec/detail/codec/stream.hpp>
#include <dacoexec/detail/huffman/archive_format.hpp>

namespace dacoexec::detail::huffman {
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
