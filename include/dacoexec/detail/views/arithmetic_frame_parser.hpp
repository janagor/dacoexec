//! DAC1 parser policy over the shared bounded frame parser.
#ifndef DACOEXEC_VIEWS_DETAIL_ARITHMETIC_FRAME_PARSER_HPP
#define DACOEXEC_VIEWS_DETAIL_ARITHMETIC_FRAME_PARSER_HPP

#include <dacoexec/detail/arithmetic/format.hpp>
#include <dacoexec/detail/codec/frame_parser.hpp>

namespace dacoexec::detail::support {
struct arithmetic_frame_format
{
  using frame_type = arithmetic::encoded_frame;
  static constexpr auto max_frame_size = arithmetic::max_frame_size;
  static constexpr auto limit_message = "Invalid DAC1 frame limit";
  static constexpr auto archive_header_size = arithmetic::archive_header_size;
  static constexpr auto frame_header_size = arithmetic::frame_header_size;
  static constexpr auto cursor_message = "DAC1 input cursor is not initialized";
  static constexpr auto truncated_message = "Truncated DAC1 archive or missing END";
  static constexpr auto sequence_message = "DAC1 sequence number space is exhausted";

  static auto read_archive_header(std::span<std::byte const, archive_header_size> bytes) -> void
  { arithmetic::read_archive_header(bytes); }

  static auto read_frame_header(std::span<std::byte const, frame_header_size> bytes, std::size_t limit)
    -> arithmetic::frame_header
  { return arithmetic::read_frame_header(bytes, limit); }

  static auto is_end(arithmetic::frame_header const &header) noexcept -> bool
  { return header.kind_ == arithmetic::frame_kind::end; }

  template<class Reader> static auto read_metadata(Reader &reader, frame_type &frame) -> void
  {
    if (frame.header_.kind_ == arithmetic::frame_kind::raw) { return; }
    auto const prefix = reader.template read_array<3>();
    auto const count = read_u16_le(std::span<std::byte const, 2>{ prefix.data() + 1, 2 });
    if (count == 0 || count > model::alphabet_size) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Invalid arithmetic model entry count");
    }
    std::vector<std::byte> metadata(prefix.begin(), prefix.end());
    for (std::size_t entry = 0; entry < count; ++entry) {
      auto const bytes = reader.template read_array<3>();
      metadata.insert(metadata.end(), bytes.begin(), bytes.end());
    }
    try {
      frame.block_.model_ = arithmetic::deserialize_model(metadata);
    } catch (::dacoexec::exception const &error) {
      // Archive metadata is wire input, even if a model constructor reports
      // an argument error. Preserve unrelated failures.
      if (error.code() != errc::invalid_payload && error.code() != errc::invalid_argument) { throw; }
      throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, error.what());
    }
    frame.block_.encoding_ = arithmetic::block_encoding::static_range;
  }
};

template<std::ranges::view V>
  requires byte_input_range<V>
using arithmetic_frame_parser = codec::frame_parser<V, arithmetic_frame_format>;
}// namespace dacoexec::detail::support
#endif
