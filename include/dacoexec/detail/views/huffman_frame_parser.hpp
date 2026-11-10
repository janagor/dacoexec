//! \file
//! DHF1 parser policy: metadata validation and format-specific diagnostics.
#ifndef DACOEXEC_VIEWS_DETAIL_HUFFMAN_FRAME_PARSER_HPP
#define DACOEXEC_VIEWS_DETAIL_HUFFMAN_FRAME_PARSER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/codec/frame_parser.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/error.hpp>
#include <span>
#include <stdexcept>

namespace dacoexec::detail::support {
struct huffman_frame_format
{
  using frame_type = huffman::encoded_frame;
  static constexpr auto archive_header_size = huffman::archive_header_size;
  static constexpr auto frame_header_size = huffman::frame_header_size;
  static constexpr auto cursor_message = "DHF1 input cursor is not initialized";
  static constexpr auto truncated_message = "Truncated DHF1 archive or missing END";
  static constexpr auto sequence_message = "dacoexec::detail::views::frames sequence number space is exhausted";

  static auto read_archive_header(std::span<std::byte const, archive_header_size> bytes) -> void
  { huffman::read_archive_header(bytes); }

  static auto read_frame_header(std::span<std::byte const, frame_header_size> bytes, std::size_t limit)
    -> huffman::frame_header
  { return huffman::read_frame_header(bytes, limit); }

  static auto is_end(huffman::frame_header const &header) noexcept -> bool
  { return header.kind_ == huffman::frame_kind::end; }

  template<class Reader> static auto read_metadata(Reader &reader, frame_type &frame) -> void
  {
    if (!(frame.header_.kind_ == huffman::frame_kind::huffman)) { return; }
    auto &block = frame.block_;
    block.encoding_ = huffman::block_encoding::canonical_huffman;
    for (auto &length : block.lengths_) { length = std::to_integer<std::uint8_t>(reader.read_byte()); }
    try {
      if (std::ranges::all_of(block.lengths_, [](auto length) -> bool { return length == 0; })
          || !huffman::try_make_canonical_codebook(block.lengths_)) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Invalid canonical lengths");
      }
    } catch (::dacoexec::exception const &error) {
      if (error.code() != errc::invalid_argument) { throw; }
      throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, error.what());
    }
  }
};

template<std::ranges::view V>
  requires byte_input_range<V>
using huffman_frame_parser = codec::frame_parser<V, huffman_frame_format>;

}// namespace dacoexec::detail::support
#endif// DACOEXEC_VIEWS_DETAIL_HUFFMAN_FRAME_PARSER_HPP
