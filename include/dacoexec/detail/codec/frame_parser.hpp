//! \file
//! Lazy, single-pass frame staging with terminal failure and checked sequencing.
#ifndef DACOEXEC_CODEC_FRAME_PARSER_HPP
#define DACOEXEC_CODEC_FRAME_PARSER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::codec {
// Format supplies frame/header types, fixed sizes, diagnostics, header validation,
// END recognition, and read_metadata(reader, frame). Only complete frames publish.
template<std::ranges::view V, class Format>
  requires byte_input_range<V>
struct frame_parser
{
  frame_parser(V source, std::size_t size) : base_(std::move(source)), frame_limit_(size) {}

  auto read_byte() -> std::byte
  {
    if (!input_.has_value()) { throw format_error(Format::cursor_message); }
    auto &cursor = input_.value();
    if (cursor == std::ranges::end(base_)) { throw format_error(Format::truncated_message); }
    using source_value = std::ranges::range_value_t<V>;
    auto const value = ::dacoexec::detail::support::to_byte(static_cast<source_value>(*cursor));
    ++cursor;
    return value;
  }

  template<std::size_t n> auto read_array() -> std::array<std::byte, n>
  {
    std::array<std::byte, n> bytes{};
    for (auto &byte : bytes) { byte = read_byte(); }
    return bytes;
  }

  auto ensure_initialized() -> void
  {
    if (initialized_) { return; }
    initialized_ = true;
    try {
      input_.emplace(std::ranges::begin(base_));
      Format::read_archive_header(read_array<Format::archive_header_size>());
      load_next_frame();
    } catch (...) {
      finished_ = true;
      throw;
    }
  }

  auto finish_archive() -> void
  {
    if (!input_.has_value()) { throw format_error(Format::cursor_message); }
    if (input_.value() != std::ranges::end(base_)) { throw format_error("Trailing bytes after END"); }
    finished_ = true;
  }

  auto read_data_block(typename Format::frame_type &frame) -> void
  {
    if (sequence_limit_reached_) { throw std::overflow_error(Format::sequence_message); }
    auto &block = frame.block_;
    block.uncompressed_size_ = frame.header_.uncompressed_size_;
    Format::read_metadata(*this, frame);
    block.payload_.resize(frame.header_.payload_size_);
    for (auto &byte : block.payload_) { byte = read_byte(); }
    block.sequence_ = next_sequence_;
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
      sequence_limit_reached_ = true;
    } else {
      ++next_sequence_;
    }
  }

  auto load_next_frame() -> void
  {
    current_.reset();
    if (finished_) { return; }
    try {
      typename Format::frame_type frame;
      frame.header_ = Format::read_frame_header(read_array<Format::frame_header_size>(), frame_limit_);
      if (Format::is_end(frame.header_)) {
        finish_archive();
        return;
      }
      read_data_block(frame);
      current_.emplace(std::move(frame));
    } catch (...) {
      finished_ = true;
      throw;
    }
  }

  // base is declared before input so the source cursor is destroyed first.
  V base_;
  std::size_t frame_limit_ = 0;
  std::optional<std::ranges::iterator_t<V>> input_;
  std::optional<typename Format::frame_type> current_;
  std::uint64_t next_sequence_ = 0;
  bool sequence_limit_reached_ = false;
  bool initialized_ = false;
  bool finished_ = false;
};
}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_FRAME_PARSER_HPP
