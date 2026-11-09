//! \file
//! Common DHF1/DAN1 frame field layout; codecs validate tags and size limits.
#ifndef DACOEXEC_CODEC_FRAME_HEADER_HPP
#define DACOEXEC_CODEC_FRAME_HEADER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/support/endian.hpp>
#include <span>

namespace dacoexec::detail::codec {
inline constexpr std::size_t size_field_offset = 1;
inline constexpr std::size_t payload_field_offset = size_field_offset + sizeof(std::uint32_t);
inline constexpr std::size_t crc_field_offset = payload_field_offset + sizeof(std::uint32_t);
inline constexpr std::size_t frame_header_size = crc_field_offset + sizeof(std::uint32_t);

// Header has kind_, uncompressed_size_, payload_size_, and crc32_ fields.
// No validation is performed here: callers must validate before publishing or writing.
template<class Header>
[[nodiscard]] constexpr auto decode_frame_header(std::span<std::byte const, frame_header_size> bytes) -> Header
{
  return { .kind_ = static_cast<decltype(Header::kind_)>(bytes.front()),
    .uncompressed_size_ = support::read_u32_le(bytes.subspan<size_field_offset, sizeof(std::uint32_t)>()),
    .payload_size_ = support::read_u32_le(bytes.subspan<payload_field_offset, sizeof(std::uint32_t)>()),
    .crc32_ = support::read_u32_le(bytes.subspan<crc_field_offset, sizeof(std::uint32_t)>()) };
}

template<class Header>
[[nodiscard]] constexpr auto encode_frame_header(Header const &header) -> std::array<std::byte, frame_header_size>
{
  std::array<std::byte, frame_header_size> bytes{};
  bytes.front() = static_cast<std::byte>(header.kind_);
  auto const put = [&bytes](std::size_t offset, std::uint32_t value) -> void {
    for (auto byte : support::write_u32_le(value)) { bytes.at(offset++) = byte; }
  };
  put(size_field_offset, header.uncompressed_size_);
  put(payload_field_offset, header.payload_size_);
  put(crc_field_offset, header.crc32_);
  return bytes;
}
}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_FRAME_HEADER_HPP
