//! \file
//! Incremental DAN1 compression and verified decompression.

#ifndef DACOEXEC_ANS_STREAM_HPP
#define DACOEXEC_ANS_STREAM_HPP

#include <dacoexec/detail/ans/block.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/detail/views/ans_frames.hpp>
#include <dacoexec/detail/views/blocks.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
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

struct stream_statistics
{
  //! Plain bytes for compression; complete archive bytes for decompression.
  std::uint64_t input_bytes_ = 0;
  //! Complete archive bytes for compression; plain bytes for decompression.
  std::uint64_t output_bytes_ = 0;
  //! Number of data frames; excludes END and the archive header.
  std::uint64_t frames_ = 0;
};

inline auto checked_add(std::uint64_t &total, std::uint64_t amount) -> void
{
  if (amount > std::numeric_limits<std::uint64_t>::max() - total) {
    throw std::overflow_error("Stream statistics overflow");
  }
  total += amount;
}

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

//! Incremental archive writer. Exceptions propagate; prior sink writes remain.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress_stream(R &&source, S &sink, stream_options options = {}) -> stream_statistics
{
  if (options.block_size_ == 0 || options.block_size_ > max_frame_size) {
    throw std::invalid_argument("Invalid block size");
  }
  validate_coder(options.coder_, options.table_log_);
  stream_statistics statistics{ .output_bytes_ = archive_header_size + frame_header_size };
  write_archive_header(sink);
  for (auto block : std::forward<R>(source) | dacoexec::detail::views::blocks(options.block_size_)) {
    auto frame = encode_frame(std::move(block), options.coder_, options.table_log_);
    ::dacoexec::detail::ans::checked_add(statistics.input_bytes_, frame.header_.uncompressed_size_);
    ::dacoexec::detail::ans::checked_add(statistics.output_bytes_,
      frame_header_size + frame.block_.payload_.size() + ::dacoexec::detail::ans::model_metadata_size(frame.block_));
    ::dacoexec::detail::ans::checked_add(statistics.frames_, 1);
    write_frame(sink, frame);
  }
  write_frame_header(sink, {});
  return statistics;
}

//! Verifies each complete frame before writing; earlier frames remain on error.
//! Size limits are checked before output allocation. END and exact EOF are required.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress_stream(R &&source, S &sink, decode_options options = {}) -> stream_statistics
{
  stream_statistics statistics{ .input_bytes_ = archive_header_size + frame_header_size };
  auto frames = std::forward<R>(source) | dacoexec::detail::views::ans_frames(options.max_frame_size_);
  for (auto cursor = frames.begin(); cursor != frames.end(); ++cursor) {
    auto frame = std::ranges::iter_move(cursor);
    if (frame.header_.uncompressed_size_ > options.max_output_bytes_ - statistics.output_bytes_) {
      throw format_error("Decompressed output exceeds configured limit");
    }
    ::dacoexec::detail::ans::checked_add(statistics.input_bytes_,
      frame_header_size + frame.block_.payload_.size() + ::dacoexec::detail::ans::model_metadata_size(frame.block_));
    ::dacoexec::detail::ans::checked_add(statistics.output_bytes_, frame.header_.uncompressed_size_);
    ::dacoexec::detail::ans::checked_add(statistics.frames_, 1);
    auto decoded = decompress_block(std::move(frame.block_), options.max_frame_size_);
    if (dacoexec::detail::support::crc32(decoded.bytes_) != frame.header_.crc32_) {
      throw format_error("DAN1 checksum mismatch");
    }
    sink.write(decoded.bytes_);
  }
  return statistics;
}
}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_STREAM_HPP
