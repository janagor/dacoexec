//! \file
//! Block acquisition and ordered archive writes for parallel codec execution.
#ifndef DACOEXEC_CODEC_PARALLEL_EXECUTION_HPP
#define DACOEXEC_CODEC_PARALLEL_EXECUTION_HPP

#include <cstddef>
#include <dacoexec/detail/codec/stream_statistics.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/views/blocks.hpp>
#include <dacoexec/error.hpp>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::codec {
// Format supplies frame/options types, size bounds, option validation, metadata
// cost, and archive/frame/END writers. Encoding is supplied separately to the sender.
// next() defers cursor advancement until requested, so active batches never read
// their successor. The ordered sender serializes initialize/next/commit/finish.
template<std::ranges::view V, io::byte_output_sink Sink, class Format> struct parallel_source
{
  using input_type = byte_block;
  using result_type = typename Format::frame_type;
  using options_type = typename Format::options_type;
  V source_;
  Sink *sink_;
  options_type options_;
  std::optional<blocks_view<V>> blocks_;
  std::optional<typename blocks_view<V>::iterator> cursor_;
  bool advance_ = false;
  stream_statistics statistics_{ .output_bytes_ = Format::archive_header_size + Format::frame_header_size };

  parallel_source(V source, Sink *sink, options_type options)
    : source_(std::move(source)), sink_(sink), options_(options)
  {}

  auto initialize() -> void
  {
    if (options_.block_size_ == 0 || options_.block_size_ > Format::max_frame_size) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid block size");
    }
    Format::validate_options(options_);
    blocks_.emplace(std::move(source_), options_.block_size_);
    Format::write_archive_header(*sink_);
  }

  auto next() -> std::optional<byte_block>
  {
    if (!blocks_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Parallel source is not initialized");
    }
    if (!cursor_) {
      cursor_.emplace(blocks_->begin());
    } else if (advance_) {
      ++*cursor_;
    }
    advance_ = false;
    if (!cursor_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Parallel source cursor is not initialized");
    }
    if (*cursor_ == blocks_->end()) { return std::nullopt; }
    advance_ = true;
    return std::optional<byte_block>{ std::in_place, std::move(**cursor_) };
  }

  auto commit(result_type frame) -> void
  {
    checked_add(statistics_.input_bytes_, frame.header_.uncompressed_size_);
    checked_add(statistics_.output_bytes_,
      Format::frame_header_size + frame.block_.payload_.size() + Format::metadata_size(frame));
    checked_add(statistics_.frames_, 1);
    Format::write_frame(*sink_, frame);
  }

  auto finish() -> stream_statistics
  {
    Format::write_end(*sink_);
    return statistics_;
  }
};

}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_PARALLEL_EXECUTION_HPP
