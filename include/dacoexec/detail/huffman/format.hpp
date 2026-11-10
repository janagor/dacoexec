//! \file
//! DHF1 archive layout, header validation, and serialization.
//!
//! DHF1 wire format shared by framing and stream operations.
//!
//! An archive begins with eight bytes: ASCII DHF1 followed by four zero bytes.
//! Each frame begins with a 13-byte header: one kind byte, then three unsigned
//! 32-bit little-endian fields (uncompressed size, payload size, CRC32).
//! RAW (0x00) carries payload_size bytes verbatim. Huffman (0x01) carries 256
//! one-byte code lengths in numeric symbol order followed by payload_size packed
//! bytes, most-significant bit first. The table is excluded from payload_size.
//! Padding has no serialized count: decoding infers fewer than eight zero low
//! bits after the declared number of output symbols. CRC32 covers uncompressed
//! bytes. END (0xFF) has three zero fields and no body; no bytes may follow it.
//!
//! Data sizes must be positive and at most max_frame_size (16 MiB), optionally
//! further restricted by a caller limit. Empty input is an archive header and END.
//! Header validation alone does not validate code lengths, payload bits, or CRC32.
//!
//! @see frames_view
//! @see compress_stream

#ifndef DACOEXEC_HUFFMAN_FORMAT_HPP
#define DACOEXEC_HUFFMAN_FORMAT_HPP

#include <dacoexec/detail/codec/frame_header.hpp>
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dacoexec::detail::huffman {
//! Default source block bound for compression: 1 MiB.
inline constexpr std::size_t default_block_size = 1U << 20U;
/**
 * Largest supported per-frame payload or uncompressed size: 16 MiB.
 */
inline constexpr std::size_t max_frame_size = 16U << 20U;
/**
 * Bytes in the magic and reserved archive prefix.
 */
inline constexpr std::size_t archive_header_size = 8;
/**
 * Bytes in the kind, two size fields and checksum header.
 */
using codec::frame_header_size;
/**
 * Offset of the uncompressed-size field in a frame header.
 */
using codec::size_field_offset;
/**
 * Offset of the payload-size field in a frame header.
 */
using codec::payload_field_offset;
/**
 * Offset of the CRC32 field in a frame header.
 */
using codec::crc_field_offset;
/**
 * Exact archive magic and zero reserved bytes accepted by read_archive_header().
 */
inline constexpr std::array<std::byte, archive_header_size> archive_header_bytes{ std::byte{ 'D' },
  std::byte{ 'H' },
  std::byte{ 'F' },
  std::byte{ '1' },
  std::byte{ 0 },
  std::byte{ 0 },
  std::byte{ 0 },
  std::byte{ 0 } };
/**
 * Serialized frame tags for RAW data, canonical Huffman data, and archive termination.
 */
enum class frame_kind : std::uint8_t { raw = 0x00, huffman = 0x01, end = 0xFF };

/**
 * DHF1 metadata in host integer representation; serialize with write_frame_header().
 */
struct frame_header
{
  //! Wire tag; END is the default empty header.
  frame_kind kind_ = frame_kind::end;
  //! Exact output bytes of this frame; zero for END.
  std::uint32_t uncompressed_size_ = 0;
  //! Packed or RAW bytes, excluding the Huffman length table; zero for END.
  std::uint32_t payload_size_ = 0;
  //! IEEE CRC32 of uncompressed bytes; zero for END.
  std::uint32_t crc32_ = 0;
};

/**
 * Move-only owner pairing wire metadata with an independently owned encoded block.
 *
 * The caller must keep header sizes and block metadata consistent for write_frame().
 */
struct encoded_frame
{
  //! Header serialized before this frame body.
  frame_header header_;
  //! Owned payload and canonical lengths; sequence is not serialized.
  encoded_block block_;
  encoded_frame() = default;
  encoded_frame(encoded_frame &&) noexcept = default;
  auto operator=(encoded_frame &&) noexcept -> encoded_frame & = default;
  encoded_frame(encoded_frame const &) = delete;
  auto operator=(encoded_frame const &) -> encoded_frame & = delete;
  ~encoded_frame() = default;
};

/**
 * Checks frame kind, nonzero data sizes, limits, RAW size equality and END zeros.
 *
 * @param limit Additional bound on both sizes, capped by max_frame_size.
 * @throws dacoexec::exception (errc::invalid_frame) If header fields violate these rules.
 * @throws dacoexec::exception (errc::output_limit_exceeded) If a configured size bound is exceeded.
 * Code lengths, payload contents, and checksum are not inspected.
 */
inline auto validate_frame_header(frame_header const &header, std::size_t limit = max_frame_size) -> void
{
  if (header.kind_ == frame_kind::end) {
    if (header.uncompressed_size_ != 0 || header.payload_size_ != 0 || header.crc32_ != 0) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Nonzero END fields");
    }
    return;
  }
  if (header.kind_ != frame_kind::raw && header.kind_ != frame_kind::huffman) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Unknown frame type");
  }
  if (header.uncompressed_size_ == 0 || header.payload_size_ == 0 || header.uncompressed_size_ > max_frame_size
      || header.payload_size_ > max_frame_size) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Invalid DHF1 frame size");
  }
  if (header.uncompressed_size_ > limit || header.payload_size_ > limit) {
    throw ::dacoexec::exception(::dacoexec::errc::output_limit_exceeded, "Frame exceeds configured size limit");
  }
  if (header.kind_ == frame_kind::raw && header.payload_size_ != header.uncompressed_size_) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Inconsistent RAW sizes");
  }
}

/**
 * Checks the complete archive magic and reserved bytes.
 *
 * @throws dacoexec::exception (errc::invalid_archive) If any byte differs from archive_header_bytes.
 */
inline auto read_archive_header(std::span<std::byte const, archive_header_size> bytes) -> void
{
  if (!std::ranges::equal(bytes, archive_header_bytes)) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_archive, "Invalid DHF1 header");
  }
}

/**
 * Reads little-endian fields from exactly one frame header and validates them.
 *
 * @param limit Additional payload and uncompressed-size bound.
 * @return Validated host-representation header, including END.
 * @throws dacoexec::exception (errc::invalid_frame) If the decoded header violates validate_frame_header().
 */
[[nodiscard]] inline auto read_frame_header(std::span<std::byte const, frame_header_size> bytes,
  std::size_t limit = max_frame_size) -> frame_header
{
  auto header = codec::decode_frame_header<frame_header>(bytes);
  validate_frame_header(header, limit);
  return header;
}

/**
 * Writes the fixed archive prefix. Sink exceptions propagate without rollback.
 */
template<io::byte_output_sink S> inline auto write_archive_header(S &sink) -> void { sink.write(archive_header_bytes); }

/**
 * Validates and writes a 13-byte little-endian header.
 *
 * @throws dacoexec::exception (errc::invalid_frame) If header fields are invalid, before writing.
 * Sink exceptions propagate; already-written bytes remain accepted.
 */
template<io::byte_output_sink S> inline auto write_frame_header(S &sink, frame_header const &header) -> void
{
  validate_frame_header(header);
  sink.write(codec::encode_frame_header(header));
}

/**
 * Writes a frame header, optional canonical length table, and owned payload.
 *
 * @throws dacoexec::exception (errc::invalid_frame) If the header is invalid or payload size, uncompressed
 *     size, or encoding disagrees with the header, before the first write.
 * Length-table structure, padding and checksum are not validated or recomputed.
 * Sink failures can leave a header or body prefix; no writes are retried.
 * Use write_frame_header(sink, {}) to emit an END header.
 *
 * @see encode_frame
 */
template<io::byte_output_sink S> inline auto write_frame(S &sink, encoded_frame const &frame) -> void
{
  validate_frame_header(frame.header_);
  auto const &block = frame.block_;
  if (block.payload_.size() != frame.header_.payload_size_
      || block.uncompressed_size_ != frame.header_.uncompressed_size_
      || block.encoding_
           != (frame.header_.kind_ == frame_kind::huffman ? block_encoding::canonical_huffman : block_encoding::raw)) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_frame, "Inconsistent encoded frame");
  }
  write_frame_header(sink, frame.header_);
  if (frame.header_.kind_ == frame_kind::huffman) {
    std::array<std::byte, model::alphabet_size> lengths{};
    std::ranges::transform(
      block.lengths_, lengths.begin(), [](auto length) -> std::byte { return static_cast<std::byte>(length); });
    sink.write(lengths);
  }
  sink.write(block.payload_);
}
}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_FORMAT_HPP
