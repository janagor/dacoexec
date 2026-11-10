//! \file
//! Incremental DHF1 compression and verified decompression.

#ifndef DACOEXEC_HUFFMAN_ARCHIVE_FORMAT_HPP
#define DACOEXEC_HUFFMAN_ARCHIVE_FORMAT_HPP

#include <dacoexec/detail/codec/stream_statistics.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/detail/views/frames.hpp>
#include <dacoexec/error.hpp>

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
 * @throws dacoexec::exception (errc::invalid_frame) If input is empty or exceeds the supported frame size.
 * Codec and allocation exceptions propagate.
 * @see compress_block
 * @see write_frame
 */
[[nodiscard]] inline auto encode_frame(byte_block block) -> encoded_frame
{
  if (block.bytes_.empty() || block.bytes_.size() > max_frame_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Invalid input block size");
  }
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


}// namespace dacoexec::detail::huffman
#endif
