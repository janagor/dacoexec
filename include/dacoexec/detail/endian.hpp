//! \file
//! Internal little-endian integer serialization.

#ifndef DACOEXEC_DETAIL_ENDIAN_HPP
#define DACOEXEC_DETAIL_ENDIAN_HPP

#include <dacoexec/detail/byte.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dacoexec::detail {
// Assemble and extract bytes explicitly so host endianness never affects
// the wire format. Internal callers use unsigned 16- or 32-bit integers; the
// uint64_t accumulator keeps their intermediate shifts representable.
template<class T> [[nodiscard]] constexpr auto read_le(std::span<std::byte const, sizeof(T)> bytes) -> T
{
  std::uint64_t value = 0;
  unsigned shift = 0;
  for (auto byte : bytes) {
    value |= std::to_integer<std::uint64_t>(byte) << shift;
    shift += bits_per_byte;
  }
  return static_cast<T>(value);
}
template<class T> [[nodiscard]] constexpr auto write_le(T value) -> std::array<std::byte, sizeof(T)>
{
  std::array<std::byte, sizeof(T)> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes.at(index) = static_cast<std::byte>(value >> (index * bits_per_byte));
  }
  return bytes;
}
[[nodiscard]] constexpr auto read_u16_le(std::span<std::byte const, sizeof(std::uint16_t)> bytes) -> std::uint16_t
{ return read_le<std::uint16_t>(bytes); }
[[nodiscard]] constexpr auto read_u32_le(std::span<std::byte const, sizeof(std::uint32_t)> bytes) -> std::uint32_t
{ return read_le<std::uint32_t>(bytes); }
[[nodiscard]] constexpr auto write_u16_le(std::uint16_t value) -> std::array<std::byte, sizeof(value)>
{ return write_le(value); }
[[nodiscard]] constexpr auto write_u32_le(std::uint32_t value) -> std::array<std::byte, sizeof(value)>
{ return write_le(value); }
}// namespace dacoexec::detail
#endif// DACOEXEC_DETAIL_ENDIAN_HPP
