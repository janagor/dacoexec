//! \file
//! DAN1 parser policy: metadata validation and format-specific diagnostics.
#ifndef DACOEXEC_VIEWS_DETAIL_ANS_FRAME_PARSER_HPP
#define DACOEXEC_VIEWS_DETAIL_ANS_FRAME_PARSER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/codec/frame_parser.hpp>
#include <span>
#include <stdexcept>

namespace dacoexec::detail::support {
struct ans_frame_format
{
  using frame_type = ans::encoded_frame;
  static constexpr auto archive_header_size = ans::archive_header_size;
  static constexpr auto frame_header_size = ans::frame_header_size;
  static constexpr auto cursor_message = "DAN1 input cursor is not initialized";
  static constexpr auto truncated_message = "Truncated DAN1 archive or missing END";
  static constexpr auto sequence_message = "dacoexec::detail::views::ans_frames sequence number space is exhausted";

  static auto read_archive_header(std::span<std::byte const, archive_header_size> bytes) -> void
  { ans::read_archive_header(bytes); }

  static auto read_frame_header(std::span<std::byte const, frame_header_size> bytes, std::size_t limit)
    -> ans::frame_header
  { return ans::read_frame_header(bytes, limit); }

  static auto is_end(ans::frame_header const &header) noexcept -> bool { return header.kind_ == ans::frame_kind::end; }

  template<class Reader> static auto read_metadata(Reader &reader, frame_type &frame) -> void
  {
    if (!(frame.header_.kind_ == ans::frame_kind::rans || frame.header_.kind_ == ans::frame_kind::tans)) { return; }
    auto &block = frame.block_;
    block.encoding_ = ans::block_encoding::rans;
    auto const precision = std::to_integer<unsigned>(reader.read_byte());
    auto const count = dacoexec::detail::support::read_u16_le(reader.template read_array<sizeof(std::uint16_t)>());
    if (count == 0 || count > ans::alphabet_size) { throw format_error("Invalid ANS model entry count"); }
    ans::frequency_model<ans::alphabet_size>::table_type frequencies{};
    unsigned previous = 0;
    for (std::uint16_t entry = 0; entry < count; ++entry) {
      auto const symbol = std::to_integer<unsigned>(reader.read_byte());
      if (entry != 0 && symbol <= previous) { throw format_error("Unordered ANS model symbols"); }
      frequencies.at(symbol) =
        std::uint32_t{ dacoexec::detail::support::read_u16_le(reader.template read_array<sizeof(std::uint16_t)>()) }
        + 1;
      previous = symbol;
    }
    try {
      block.model_.emplace(frequencies, precision);
    } catch (std::invalid_argument const &error) {
      throw format_error(error.what());
    }
    block.encoding_ = static_cast<ans::block_encoding>(frame.header_.kind_);
  }
};

template<std::ranges::view V>
  requires byte_input_range<V>
using ans_frame_parser = codec::frame_parser<V, ans_frame_format>;

}// namespace dacoexec::detail::support
#endif// DACOEXEC_VIEWS_DETAIL_ANS_FRAME_PARSER_HPP
