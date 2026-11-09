//! \file
#ifndef DACOEXEC_VIEWS_DETAIL_ANS_FRAME_PARSER_HPP
#define DACOEXEC_VIEWS_DETAIL_ANS_FRAME_PARSER_HPP

#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
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
struct ans_frame_parser
{
  ans_frame_parser(V source, std::size_t size) : base_(std::move(source)), frame_limit_(size) {}

  auto read_byte() -> std::byte
  {
    if (!input_.has_value()) { throw format_error("DAN1 input cursor is not initialized"); }
    auto &cursor = input_.value();
    if (cursor == std::ranges::end(base_)) { throw format_error("Truncated DAN1 archive or missing END"); }
    using source_value = std::ranges::range_value_t<V>;
    auto const value = ::dacoexec::detail::support::to_byte(static_cast<source_value>(*cursor));
    ++cursor;
    return value;
  }

  template<std::size_t n> auto read_array() -> std::array<std::byte, n>
  {
    std::array<std::byte, n> bytes{};
    std::ranges::generate(bytes, [this]() -> std::byte { return read_byte(); });
    return bytes;
  }

  auto ensure_initialized() -> void
  {
    if (initialized_) { return; }
    initialized_ = true;
    try {
      input_.emplace(std::ranges::begin(base_));
      ans::read_archive_header(read_array<ans::archive_header_size>());
      load_next_frame();
    } catch (...) {
      finished_ = true;
      throw;
    }
  }

  auto finish_archive() -> void
  {
    if (!input_.has_value()) { throw format_error("DAN1 input cursor is not initialized"); }
    if (input_.value() != std::ranges::end(base_)) { throw format_error("Trailing bytes after END"); }
    finished_ = true;
  }

  auto read_ans_model(ans::encoded_block &block) -> void
  {
    block.encoding_ = ans::block_encoding::rans;
    auto const precision = std::to_integer<unsigned>(read_byte());
    auto const count = dacoexec::detail::support::read_u16_le(read_array<sizeof(std::uint16_t)>());
    if (count == 0 || count > ans::alphabet_size) { throw format_error("Invalid ANS model entry count"); }
    ans::frequency_model<ans::alphabet_size>::table_type frequencies{};
    unsigned previous = 0;
    for (std::uint16_t entry = 0; entry < count; ++entry) {
      auto const symbol = std::to_integer<unsigned>(read_byte());
      if (entry != 0 && symbol <= previous) { throw format_error("Unordered ANS model symbols"); }
      frequencies.at(symbol) =
        std::uint32_t{ dacoexec::detail::support::read_u16_le(read_array<sizeof(std::uint16_t)>()) } + 1;
      previous = symbol;
    }
    try {
      block.model_.emplace(frequencies, precision);
    } catch (std::invalid_argument const &error) {
      throw format_error(error.what());
    }
  }

  auto read_data_block(ans::encoded_frame &frame) -> void
  {
    if (sequence_limit_reached_) {
      throw std::overflow_error("dacoexec::detail::views::ans_frames sequence number space is exhausted");
    }
    auto &block = frame.block_;
    block.uncompressed_size_ = frame.header_.uncompressed_size_;
    if (frame.header_.kind_ == ans::frame_kind::rans) { read_ans_model(block); }
    block.payload_.resize(frame.header_.payload_size_);
    std::ranges::generate(block.payload_, [this]() -> std::byte { return read_byte(); });
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
      ans::encoded_frame frame;
      frame.header_ = ans::read_frame_header(read_array<ans::frame_header_size>(), frame_limit_);
      if (frame.header_.kind_ == ans::frame_kind::end) {
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
  std::optional<ans::encoded_frame> current_;
  std::uint64_t next_sequence_ = 0;
  bool sequence_limit_reached_ = false;
  bool initialized_ = false;
  bool finished_ = false;
};
}// namespace dacoexec::detail::support
#endif
