//! \file
#ifndef DACOEXEC_VIEWS_DETAIL_HUFFMAN_FRAME_PARSER_HPP
#define DACOEXEC_VIEWS_DETAIL_HUFFMAN_FRAME_PARSER_HPP

#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec::detail::support {
template<std::ranges::view V>
  requires byte_input_range<V>
struct huffman_frame_parser
{
  huffman_frame_parser(V source, std::size_t size) : base_(std::move(source)), frame_limit_(size) {}

  auto read_byte() -> std::byte
  {
    if (!input_.has_value()) { throw format_error("DHF1 input cursor is not initialized"); }
    auto &cursor = input_.value();
    if (cursor == std::ranges::end(base_)) { throw format_error("Truncated DHF1 archive or missing END"); }
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
      huffman::read_archive_header(read_array<huffman::archive_header_size>());
      load_next_frame();
    } catch (...) {
      finished_ = true;
      throw;
    }
  }

  auto finish_archive() -> void
  {
    if (!input_.has_value()) { throw format_error("DHF1 input cursor is not initialized"); }
    if (input_.value() != std::ranges::end(base_)) { throw format_error("Trailing bytes after END"); }
    finished_ = true;
  }

  auto read_huffman_lengths(huffman::encoded_block &block) -> void
  {
    block.encoding_ = huffman::block_encoding::canonical_huffman;
    for (auto &length : block.lengths_) { length = std::to_integer<std::uint8_t>(read_byte()); }
    try {
      if (std::ranges::all_of(block.lengths_, [](auto length) -> bool { return length == 0; })
          || !huffman::try_make_canonical_codebook(block.lengths_)) {
        throw format_error("Invalid canonical lengths");
      }
    } catch (std::invalid_argument const &error) {
      throw format_error(error.what());
    }
  }

  auto read_data_block(huffman::encoded_frame &frame) -> void
  {
    if (sequence_limit_reached_) {
      throw std::overflow_error("dacoexec::detail::views::frames sequence number space is exhausted");
    }
    auto &block = frame.block_;
    block.uncompressed_size_ = frame.header_.uncompressed_size_;
    if (frame.header_.kind_ == huffman::frame_kind::huffman) { read_huffman_lengths(block); }
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
      huffman::encoded_frame frame;
      frame.header_ = huffman::read_frame_header(read_array<huffman::frame_header_size>(), frame_limit_);
      if (frame.header_.kind_ == huffman::frame_kind::end) {
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
  std::optional<huffman::encoded_frame> current_;
  std::uint64_t next_sequence_ = 0;
  bool sequence_limit_reached_ = false;
  bool initialized_ = false;
  bool finished_ = false;
};
}// namespace dacoexec::detail::support
#endif
