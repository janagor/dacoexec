//! \file
//! Shared incremental archive compression and verified decompression.
#ifndef DACOEXEC_CODEC_STREAM_HPP
#define DACOEXEC_CODEC_STREAM_HPP

#include <cstddef>
#include <dacoexec/detail/codec/stream_statistics.hpp>
#include <dacoexec/detail/codec/verified_frame.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/views/blocks.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::codec {

// Format owns codec options, frame parsing, encoding/decoding, metadata costs,
// diagnostics, and serialization. These loops own ordering and stream accounting.
template<class Format, std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress_stream(R &&source, S &sink, typename Format::options_type const &options)
  -> stream_statistics
{
  if (options.block_size_ == 0 || options.block_size_ > Format::max_frame_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid block size");
  }
  Format::validate_options(options);
  stream_statistics statistics{ .output_bytes_ = Format::archive_header_size + Format::frame_header_size };
  Format::write_archive_header(sink);
  for (auto block : std::forward<R>(source) | views::blocks(options.block_size_)) {
    auto frame = Format::encode_frame(std::move(block), options);
    checked_add(statistics.input_bytes_, frame.header_.uncompressed_size_);
    checked_add(statistics.output_bytes_,
      Format::frame_header_size + frame.block_.payload_.size() + Format::metadata_size(frame));
    checked_add(statistics.frames_, 1);
    Format::write_frame(sink, frame);
  }
  Format::write_end(sink);
  return statistics;
}

template<class Format, std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress_stream(R &&source, S &sink, typename Format::decode_options_type const &options)
  -> stream_statistics
{
  stream_statistics statistics{ .input_bytes_ = Format::archive_header_size + Format::frame_header_size };
  auto frames = Format::frames(std::forward<R>(source), options.max_frame_size_);
  for (auto cursor = frames.begin(); cursor != frames.end(); ++cursor) {
    // Both parsers transfer ownership through iter_move, even when ordinary
    // dereference borrows a const frame. Decode before advancing the parser.
    auto frame = std::ranges::iter_move(cursor);
    auto const archive_bytes = Format::frame_header_size + frame.block_.payload_.size() + Format::metadata_size(frame);
    auto decoded = decode_verified_frame<Format>(frame, options, statistics.output_bytes_);
    checked_add(statistics.input_bytes_, archive_bytes);
    checked_add(statistics.output_bytes_, frame.header_.uncompressed_size_);
    checked_add(statistics.frames_, 1);
    sink.write(decoded.bytes_);
  }
  return statistics;
}

}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_STREAM_HPP
