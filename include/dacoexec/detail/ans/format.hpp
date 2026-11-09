//! \file
//! DAN1: eight-byte magic (DAN1 and four zero bytes), followed by frames.
//! Each frame has a tag and little-endian uint32 output size, payload size,
//! and uncompressed IEEE CRC32. Tags: RAW=0, rANS=1, tANS=2, END=255. END fields
//! are zero and no bytes follow. rANS carries one precision byte, a uint16 entry count, and sorted
//! (uint8 symbol, uint16 frequency-minus-one) entries before its payload (excluded from payload size).
//! Payload includes the little-endian decoder state. Sizes are bounded to 16 MiB.

#ifndef DACOEXEC_ANS_FORMAT_HPP
#define DACOEXEC_ANS_FORMAT_HPP

#include <dacoexec/detail/ans/block.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dacoexec::detail::ans {
//! Default source block bound for compression: 1 MiB.
inline constexpr std::size_t default_block_size = 1U << 20U;
inline constexpr std::size_t max_frame_size = 16U << 20U;
inline constexpr std::size_t archive_header_size = 8;
inline constexpr std::size_t frame_header_size = 13;
inline constexpr std::size_t size_field_offset = 1;
inline constexpr std::size_t payload_field_offset = size_field_offset + sizeof(std::uint32_t);
inline constexpr std::size_t crc_field_offset = payload_field_offset + sizeof(std::uint32_t);
inline constexpr std::array<std::byte, archive_header_size> archive_header_bytes{ std::byte{ 'D' },
  std::byte{ 'A' },
  std::byte{ 'N' },
  std::byte{ '1' },
  std::byte{ 0 },
  std::byte{ 0 },
  std::byte{ 0 },
  std::byte{ 0 } };
enum class frame_kind : std::uint8_t { raw = 0x00, rans = 0x01, tans = 0x02, end = 0xFF };

struct frame_header
{
  //! Wire tag; END is the default empty header.
  frame_kind kind_ = frame_kind::end;
  //! Exact output bytes of this frame; zero for END.
  std::uint32_t uncompressed_size_ = 0;
  //! rANS or RAW bytes, excluding the normalized model; zero for END.
  std::uint32_t payload_size_ = 0;
  //! IEEE CRC32 of uncompressed bytes; zero for END.
  std::uint32_t crc32_ = 0;
};

struct encoded_frame
{
  //! Header serialized before this frame body.
  frame_header header_;
  //! Owned payload and normalized model; sequence is not serialized.
  encoded_block block_;
  encoded_frame() = default;
  encoded_frame(encoded_frame &&) noexcept = default;
  auto operator=(encoded_frame &&) noexcept -> encoded_frame & = default;
  encoded_frame(encoded_frame const &) = delete;
  auto operator=(encoded_frame const &) -> encoded_frame & = delete;
  ~encoded_frame() = default;
};

inline auto validate_frame_header(frame_header const &header, std::size_t limit = max_frame_size) -> void
{
  if (header.kind_ == frame_kind::end) {
    if (header.uncompressed_size_ != 0 || header.payload_size_ != 0 || header.crc32_ != 0) {
      throw format_error("Nonzero END fields");
    }
    return;
  }
  if (header.kind_ != frame_kind::raw && header.kind_ != frame_kind::rans && header.kind_ != frame_kind::tans) {
    throw format_error("Unknown frame type");
  }
  if (header.uncompressed_size_ == 0 || header.payload_size_ == 0 || header.uncompressed_size_ > limit
      || header.payload_size_ > limit || header.uncompressed_size_ > max_frame_size
      || header.payload_size_ > max_frame_size) {
    throw format_error("Invalid DAN1 frame size");
  }
  if (header.kind_ == frame_kind::rans && header.payload_size_ < ::dacoexec::detail::ans::state_bytes) {
    throw format_error("Truncated ANS state");
  }
  if (header.kind_ == frame_kind::tans && header.payload_size_ < tans::payload_prefix_size) {
    throw format_error("Truncated tANS state and bit count");
  }
  if (header.kind_ == frame_kind::raw && header.payload_size_ != header.uncompressed_size_) {
    throw format_error("Inconsistent RAW sizes");
  }
}

inline auto read_archive_header(std::span<std::byte const, archive_header_size> bytes) -> void
{
  if (!std::ranges::equal(bytes, archive_header_bytes)) { throw format_error("Invalid DAN1 header"); }
}

[[nodiscard]] inline auto read_frame_header(std::span<std::byte const, frame_header_size> bytes,
  std::size_t limit = max_frame_size) -> frame_header
{
  frame_header header{ .kind_ = static_cast<frame_kind>(bytes.front()),
    .uncompressed_size_ =
      dacoexec::detail::support::read_u32_le(bytes.subspan<size_field_offset, sizeof(std::uint32_t)>()),
    .payload_size_ =
      dacoexec::detail::support::read_u32_le(bytes.subspan<payload_field_offset, sizeof(std::uint32_t)>()),
    .crc32_ = dacoexec::detail::support::read_u32_le(bytes.subspan<crc_field_offset, sizeof(std::uint32_t)>()) };
  validate_frame_header(header, limit);
  return header;
}

template<io::byte_output_sink S> inline auto write_archive_header(S &sink) -> void { sink.write(archive_header_bytes); }

template<io::byte_output_sink S> inline auto write_frame_header(S &sink, frame_header const &header) -> void
{
  validate_frame_header(header);
  std::array<std::byte, frame_header_size> bytes{};
  bytes.front() = static_cast<std::byte>(header.kind_);
  auto const put = [&bytes](std::size_t offset, std::uint32_t value) -> void {
    auto const encoded = dacoexec::detail::support::write_u32_le(value);
    for (auto byte : encoded) { bytes.at(offset++) = byte; }
  };
  put(size_field_offset, header.uncompressed_size_);
  put(payload_field_offset, header.payload_size_);
  put(crc_field_offset, header.crc32_);
  sink.write(bytes);
}

template<io::byte_output_sink S> inline auto write_frame(S &sink, encoded_frame const &frame) -> void
{
  validate_frame_header(frame.header_);
  if (frame.header_.kind_ == frame_kind::end) { throw format_error("END has no data block"); }
  auto const &block = frame.block_;
  if (block.payload_.size() != frame.header_.payload_size_
      || block.uncompressed_size_ != frame.header_.uncompressed_size_
      || block.encoding_ != (static_cast<block_encoding>(frame.header_.kind_))) {
    throw format_error("Inconsistent encoded frame");
  }
  if ((block.encoding_ != block_encoding::raw) != block.model_.has_value()) {
    throw format_error("Inconsistent ANS model metadata");
  }
  write_frame_header(sink, frame.header_);
  if (frame.header_.kind_ == frame_kind::rans || frame.header_.kind_ == frame_kind::tans) {
    if (!block.model_) { throw format_error("Missing ANS model"); }
    constexpr auto max_model_wire_size =
      ::dacoexec::detail::ans::model_prefix_size + (alphabet_size * ::dacoexec::detail::ans::model_entry_size);
    std::array<std::byte, max_model_wire_size> metadata{};
    std::size_t position = 0;
    auto const append = [&metadata, &position](auto const &bytes) -> void {
      for (auto byte : bytes) { metadata.at(position++) = byte; }
    };
    metadata.at(position++) = static_cast<std::byte>(block.model_->precision_bits());
    auto const count =
      (::dacoexec::detail::ans::model_wire_size(*block.model_) - ::dacoexec::detail::ans::model_prefix_size)
      / ::dacoexec::detail::ans::model_entry_size;
    append(dacoexec::detail::support::write_u16_le(static_cast<std::uint16_t>(count)));
    for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
      auto const frequency = block.model_->frequencies().at(symbol);
      if (frequency == 0) { continue; }
      metadata.at(position++) = static_cast<std::byte>(symbol);
      append(dacoexec::detail::support::write_u16_le(static_cast<std::uint16_t>(frequency - 1)));
    }
    sink.write(std::span<std::byte const>{ metadata }.first(position));
  }
  sink.write(block.payload_);
}
}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_FORMAT_HPP
