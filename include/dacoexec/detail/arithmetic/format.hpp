//! \file
//! DAC1: eight-byte magic (DAC1 and four zero bytes), followed by frames.
//! Each frame has a tag and little-endian uint32 output size, payload size,
//! and uncompressed IEEE CRC32. RAW=0, static range=1, adaptive range=2, binary=3, reserved context binary=4,
//! END=255. END fields are zero and no bytes follow. Static range stores a
//! precision byte, LE uint16 entry count, and sorted (symbol, LE uint16
//! frequency-minus-one) entries before the payload, excluded from payload size.
//! Adaptive range has no model metadata: all 256 frequencies start at one,
//! update after each symbol, and rescale above 32768 by rounding halves upward.
//! Binary frames store two LE uint16 frequency-minus-one values before their
//! payload, excluded from its size. The sum must be <= 65536. Exactly eight
//! MSB-first bits per output byte use the binary engine termination contract.
//! Range payloads use the five-byte initialization/finalization contract of the
//! range engine. Both sizes are bounded to 16 MiB; models reset at each frame.

#ifndef DACOEXEC_ARITHMETIC_FORMAT_HPP
#define DACOEXEC_ARITHMETIC_FORMAT_HPP

#include <dacoexec/detail/arithmetic/binary_arithmetic_codec.hpp>
#include <dacoexec/detail/arithmetic/block.hpp>
#include <dacoexec/detail/arithmetic/range_common.hpp>
#include <dacoexec/detail/codec/frame_header.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dacoexec::detail::arithmetic {
//! Default source block bound for compression: 1 MiB.
inline constexpr std::size_t default_block_size = 1U << 20U;
inline constexpr std::size_t max_frame_size = 16U << 20U;
inline constexpr std::size_t archive_header_size = 8;
using codec::frame_header_size;
using codec::size_field_offset;
using codec::payload_field_offset;
using codec::crc_field_offset;
inline constexpr std::array<std::byte, archive_header_size> archive_header_bytes{ std::byte{ 'D' },
  std::byte{ 'A' },
  std::byte{ 'C' },
  std::byte{ '1' },
  std::byte{ 0 },
  std::byte{ 0 },
  std::byte{ 0 },
  std::byte{ 0 } };
enum class frame_kind : std::uint8_t {
  raw = 0x00,
  static_range = 0x01,
  adaptive_range = 0x02,
  binary = 0x03,
  context_binary = 0x04,
  end = 0xFF
};

struct frame_header
{
  //! Wire tag; END is the default empty header.
  frame_kind kind_ = frame_kind::end;
  //! Exact output bytes of this frame; zero for END.
  std::uint32_t uncompressed_size_ = 0;
  //! Compressed or RAW bytes, excluding model metadata; zero for END.
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
      throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Nonzero END fields");
    }
    return;
  }
  if (header.kind_ == frame_kind::context_binary) {
    throw ::dacoexec::exception(::dacoexec::errc::unsupported_coder, "Unsupported arithmetic entropy coder");
  }
  if (header.kind_ != frame_kind::raw && header.kind_ != frame_kind::static_range
      && header.kind_ != frame_kind::adaptive_range && header.kind_ != frame_kind::binary) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Unknown frame type");
  }
  if (header.uncompressed_size_ == 0 || header.payload_size_ == 0 || header.uncompressed_size_ > max_frame_size
      || header.payload_size_ > max_frame_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Invalid DAC1 frame size");
  }
  if (header.uncompressed_size_ > limit || header.payload_size_ > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "Frame exceeds configured size limit");
  }
  if ((header.kind_ == frame_kind::static_range || header.kind_ == frame_kind::adaptive_range)
      && header.payload_size_ < range_flush_bytes) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Truncated range state");
  }
  if (header.kind_ == frame_kind::binary && header.payload_size_ < binary_min_payload_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Truncated binary state");
  }
  if (header.kind_ == frame_kind::raw && header.payload_size_ != header.uncompressed_size_) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Inconsistent RAW sizes");
  }
}

inline auto read_archive_header(std::span<std::byte const, archive_header_size> bytes) -> void
{
  if (!std::ranges::equal(bytes, archive_header_bytes)) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_archive, "Invalid DAC1 header");
  }
}

[[nodiscard]] inline auto read_frame_header(std::span<std::byte const, frame_header_size> bytes,
  std::size_t limit = max_frame_size) -> frame_header
{
  auto header = codec::decode_frame_header<frame_header>(bytes);
  validate_frame_header(header, limit);
  return header;
}

template<io::byte_output_sink S> inline auto write_archive_header(S &sink) -> void { sink.write(archive_header_bytes); }

template<io::byte_output_sink S> inline auto write_frame_header(S &sink, frame_header const &header) -> void
{
  validate_frame_header(header);
  sink.write(codec::encode_frame_header(header));
}

template<io::byte_output_sink S> inline auto write_frame(S &sink, encoded_frame const &frame) -> void
{
  validate_frame_header(frame.header_);
  if (frame.header_.kind_ == frame_kind::end) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "END has no data block");
  }
  auto const &block = frame.block_;
  if (block.payload_.size() != frame.header_.payload_size_
      || block.uncompressed_size_ != frame.header_.uncompressed_size_
      || block.encoding_ != (static_cast<block_encoding>(frame.header_.kind_))) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Inconsistent encoded frame");
  }
  if (((block.encoding_ == block_encoding::static_range) != block.model_.has_value())
      || ((block.encoding_ == block_encoding::binary) != block.binary_model_.has_value())) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Inconsistent arithmetic model metadata");
  }
  if (block.binary_model_) { static_cast<void>(block.binary_model_->total()); }
  write_frame_header(sink, frame.header_);
  if (frame.header_.kind_ == frame_kind::static_range) {
    auto const metadata = serialize_model(*block.model_);
    sink.write(metadata);
  }
  if (frame.header_.kind_ == frame_kind::binary) { sink.write(serialize_binary_model(*block.binary_model_)); }
  sink.write(block.payload_);
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_FORMAT_HPP
