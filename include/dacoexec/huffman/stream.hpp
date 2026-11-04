#ifndef DACOEXEC_HUFFMAN_STREAM_HPP
#define DACOEXEC_HUFFMAN_STREAM_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/crc32.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/format.hpp>
#include <dacoexec/io/output_sink.hpp>
#include <dacoexec/views/blocks.hpp>
#include <dacoexec/views/frames.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::huffman {
struct stream_options
{
  std::size_t block_size_ = default_block_size;
};
struct decode_options
{
  std::size_t max_frame_size_ = huffman::max_frame_size;
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
};
struct stream_statistics
{
  std::uint64_t input_bytes_ = 0;
  std::uint64_t output_bytes_ = 0;
  std::uint64_t frames_ = 0;
};
namespace detail {
  inline auto checked_add(std::uint64_t &total, std::uint64_t amount) -> void
  {
    if (amount > std::numeric_limits<std::uint64_t>::max() - total) { throw std::overflow_error("Stream statistics overflow"); }
    total += amount;
  }
}// namespace detail
// Encoding, frame ownership and serialization are independent of scheduling.
[[nodiscard]] inline auto encode_frame(byte_block block) -> encoded_frame
{
  if (block.bytes_.empty() || block.bytes_.size() > max_frame_size) { throw format_error("Invalid input block size"); }
  auto const checksum = dacoexec::detail::crc32(block.bytes_);
  encoded_frame frame;
  frame.block_ = compress_block(std::move(block));
  frame.header_ = { .kind_ = frame.block_.encoding_ == block_encoding::raw ? frame_kind::raw : frame_kind::huffman,
    .uncompressed_size_ = static_cast<std::uint32_t>(frame.block_.uncompressed_size_),
    .payload_size_ = static_cast<std::uint32_t>(frame.block_.payload_.size()), .crc32_ = checksum };
  return frame;
}
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress_stream(R &&source, S &sink, stream_options options = {}) -> stream_statistics
{
  if (options.block_size_ == 0 || options.block_size_ > max_frame_size) { throw std::invalid_argument("Invalid block size"); }
  stream_statistics statistics{ .output_bytes_ = archive_header_size + frame_header_size };
  write_archive_header(sink);
  for (auto block : std::forward<R>(source) | dacoexec::views::blocks(options.block_size_)) {
    auto frame = encode_frame(std::move(block));
    detail::checked_add(statistics.input_bytes_, frame.header_.uncompressed_size_);
    detail::checked_add(statistics.output_bytes_, frame_header_size + frame.block_.payload_.size()
      + (frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U));
    detail::checked_add(statistics.frames_, 1);
    write_frame(sink, frame);
  }
  write_frame_header(sink, {});
  return statistics;
}
// Earlier frames remain written if a later frame fails. Current frame content
// and output limits are validated before its first write; sinks cannot roll back.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress_stream(R &&source, S &sink, decode_options options = {}) -> stream_statistics
{
  stream_statistics statistics{ .input_bytes_ = archive_header_size + frame_header_size };
  for (auto frame : std::forward<R>(source) | dacoexec::views::frames(options.max_frame_size_)) {
    if (frame.header_.uncompressed_size_ > options.max_output_bytes_ - statistics.output_bytes_) {
      throw format_error("Decompressed output exceeds configured limit");
    }
    detail::checked_add(statistics.input_bytes_, frame_header_size + frame.block_.payload_.size()
      + (frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U));
    detail::checked_add(statistics.output_bytes_, frame.header_.uncompressed_size_);
    detail::checked_add(statistics.frames_, 1);
    auto decoded = decompress_block(std::move(frame.block_), options.max_frame_size_, true);
    if (dacoexec::detail::crc32(decoded.bytes_) != frame.header_.crc32_) { throw format_error("DHF1 checksum mismatch"); }
    sink.write(decoded.bytes_);
  }
  return statistics;
}
}// namespace dacoexec::huffman
#endif // DACOEXEC_HUFFMAN_STREAM_HPP
