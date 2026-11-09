//! \file
//! Incremental DAN1 compression and verified decompression.

#ifndef DACOEXEC_ANS_STREAM_HPP
#define DACOEXEC_ANS_STREAM_HPP

#include <dacoexec/detail/ans/block.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/codec/stream.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/detail/views/ans_frames.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>

namespace dacoexec::detail::ans {
struct stream_options
{
  //! Maximum source bytes per data frame; must be in [1, max_frame_size].
  std::size_t block_size_ = default_block_size;
  entropy_coder coder_ = entropy_coder::rans;
  unsigned table_log_ = tans::default_table_log;
};

struct decode_options
{
  //! Limit on each payload and uncompressed size; must be in [1, max_frame_size].
  std::size_t max_frame_size_ = ans::max_frame_size;
  //! Maximum total decoded bytes; zero permits only an archive without data frames.
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
};

using codec::stream_statistics;
using codec::checked_add;

//! Computes CRC32 before consuming a nonempty bounded block.
[[nodiscard]] inline auto encode_frame(byte_block block,
  entropy_coder coder = entropy_coder::rans,
  unsigned table_log = tans::default_table_log) -> encoded_frame
{
  if (block.bytes_.empty() || block.bytes_.size() > max_frame_size) { throw format_error("Invalid input block size"); }
  auto const checksum = dacoexec::detail::support::crc32(block.bytes_);
  encoded_frame frame;
  frame.block_ = encoder{ coder, table_log }.encode(std::move(block));
  frame.header_ = { .kind_ = static_cast<frame_kind>(frame.block_.encoding_),
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
  static constexpr auto archive_header_size = ans::archive_header_size;
  static constexpr auto frame_header_size = ans::frame_header_size;
  static constexpr auto max_frame_size = ans::max_frame_size;

  template<class Options> static auto validate_options(Options const &options) -> void
  { validate_coder(options.coder_, options.table_log_); }

  [[nodiscard]] static auto metadata_size(encoded_frame const &frame) -> std::size_t
  { return ::dacoexec::detail::ans::model_metadata_size(frame.block_); }

  template<io::byte_output_sink Sink> static auto write_archive_header(Sink &sink) -> void
  { ans::write_archive_header(sink); }

  template<io::byte_output_sink Sink> static auto write_frame(Sink &sink, encoded_frame const &frame) -> void
  { ans::write_frame(sink, frame); }

  template<io::byte_output_sink Sink> static auto write_end(Sink &sink) -> void { ans::write_frame_header(sink, {}); }

  [[nodiscard]] static auto encode_frame(byte_block block, stream_options const &options) -> encoded_frame
  { return ans::encode_frame(std::move(block), options.coder_, options.table_log_); }

  template<std::ranges::viewable_range R> [[nodiscard]] static auto frames(R &&source, std::size_t limit)
  { return std::forward<R>(source) | ::dacoexec::detail::views::ans_frames(limit); }

  [[nodiscard]] static auto decode_block(encoded_block block, std::size_t limit) -> byte_block
  { return ans::decompress_block(std::move(block), limit); }

  [[nodiscard]] static auto checksum_message() noexcept -> char const * { return "DAN1 checksum mismatch"; }
};

//! Incremental archive writer. Exceptions propagate; prior sink writes remain.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress_stream(R &&source, S &sink, stream_options options = {}) -> stream_statistics
{ return codec::compress_stream<stream_format>(std::forward<R>(source), sink, options); }

//! Verifies each complete frame before writing; earlier frames remain on error.
//! Size limits are checked before output allocation. END and exact EOF are required.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress_stream(R &&source, S &sink, decode_options options = {}) -> stream_statistics
{ return codec::decompress_stream<stream_format>(std::forward<R>(source), sink, options); }
}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_STREAM_HPP
