#ifndef DACOEXEC_HUFFMAN_FORMAT_HPP
#define DACOEXEC_HUFFMAN_FORMAT_HPP

#include <dacoexec/detail/endian.hpp>
#include <dacoexec/detail/error.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/io/output_sink.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dacoexec::huffman {
inline constexpr std::size_t default_block_size = 1U << 20U;
inline constexpr std::size_t max_frame_size = 16U << 20U;
inline constexpr std::size_t archive_header_size = 8;
inline constexpr std::size_t frame_header_size = 13;
inline constexpr std::size_t size_field_offset = 1;
inline constexpr std::size_t payload_field_offset = size_field_offset + sizeof(std::uint32_t);
inline constexpr std::size_t crc_field_offset = payload_field_offset + sizeof(std::uint32_t);
inline constexpr std::array<std::byte, archive_header_size> archive_header_bytes{
  std::byte{'D'}, std::byte{'H'}, std::byte{'F'}, std::byte{'1'},
  std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}
};
enum class frame_kind : std::uint8_t { raw = 0x00, huffman = 0x01, end = 0xFF };
struct frame_header
{
  frame_kind kind_ = frame_kind::end;
  std::uint32_t uncompressed_size_ = 0;
  std::uint32_t payload_size_ = 0;
  std::uint32_t crc32_ = 0;
};
struct encoded_frame
{
  frame_header header_;
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
  if (header.kind_ != frame_kind::raw && header.kind_ != frame_kind::huffman) { throw format_error("Unknown frame type"); }
  if (header.uncompressed_size_ == 0 || header.payload_size_ == 0
      || header.uncompressed_size_ > limit || header.payload_size_ > limit
      || header.uncompressed_size_ > max_frame_size || header.payload_size_ > max_frame_size) {
    throw format_error("Invalid DHF1 frame size");
  }
  if (header.kind_ == frame_kind::raw && header.payload_size_ != header.uncompressed_size_) {
    throw format_error("Inconsistent RAW sizes");
  }
}
inline auto read_archive_header(std::span<const std::byte, archive_header_size> bytes) -> void
{
  if (!std::ranges::equal(bytes, archive_header_bytes)) { throw format_error("Invalid DHF1 header"); }
}
[[nodiscard]] inline auto read_frame_header(std::span<const std::byte, frame_header_size> bytes,
                                          std::size_t limit = max_frame_size) -> frame_header
{
  frame_header header{ .kind_ = static_cast<frame_kind>(bytes.front()),
    .uncompressed_size_ = dacoexec::detail::read_u32_le(bytes.subspan<size_field_offset, sizeof(std::uint32_t)>()),
    .payload_size_ = dacoexec::detail::read_u32_le(bytes.subspan<payload_field_offset, sizeof(std::uint32_t)>()),
    .crc32_ = dacoexec::detail::read_u32_le(bytes.subspan<crc_field_offset, sizeof(std::uint32_t)>()) };
  validate_frame_header(header, limit);
  return header;
}
template<io::byte_output_sink S>
inline auto write_archive_header(S &sink) -> void { sink.write(archive_header_bytes); }
template<io::byte_output_sink S>
inline auto write_frame_header(S &sink, frame_header const &header) -> void
{
  validate_frame_header(header);
  std::array<std::byte, frame_header_size> bytes{};
  bytes.front() = static_cast<std::byte>(header.kind_);
  auto const put = [&bytes](std::size_t offset, std::uint32_t value) -> void {
    auto const encoded = dacoexec::detail::write_u32_le(value);
    for (auto byte : encoded) { bytes.at(offset++) = byte; }
  };
  put(size_field_offset, header.uncompressed_size_);
  put(payload_field_offset, header.payload_size_);
  put(crc_field_offset, header.crc32_);
  sink.write(bytes);
}
template<io::byte_output_sink S>
inline auto write_frame(S &sink, encoded_frame const &frame) -> void
{
  validate_frame_header(frame.header_);
  auto const &block = frame.block_;
  if (block.payload_.size() != frame.header_.payload_size_ || block.uncompressed_size_ != frame.header_.uncompressed_size_
      || block.encoding_ != (frame.header_.kind_ == frame_kind::huffman ? block_encoding::canonical_huffman : block_encoding::raw)) {
    throw format_error("Inconsistent encoded frame");
  }
  write_frame_header(sink, frame.header_);
  if (frame.header_.kind_ == frame_kind::huffman) {
    std::array<std::byte, alphabet_size> lengths{};
    std::ranges::transform(block.lengths_, lengths.begin(), [](auto length) -> std::byte { return static_cast<std::byte>(length); });
    sink.write(lengths);
  }
  sink.write(block.payload_);
}
}// namespace dacoexec::huffman
#endif // DACOEXEC_HUFFMAN_FORMAT_HPP
