//! \file
//! Internal reflected IEEE CRC32 computation.

#ifndef DACOEXEC_DETAIL_CRC32_HPP
#define DACOEXEC_DETAIL_CRC32_HPP

#include <dacoexec/detail/byte.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace dacoexec::detail {
// Reflected IEEE CRC32 (normal polynomial 0x04C11DB7): process low bits
// first, start with all ones, and XOR with all ones after the final byte.
// The empty sequence therefore has checksum zero.
inline constexpr auto crc32_polynomial = std::uint32_t{ 0xEDB88320U };
inline constexpr auto crc32_mask = std::numeric_limits<std::uint32_t>::max();
inline constexpr auto crc32_table_size = std::size_t{ 1 } << bits_per_byte;
inline constexpr auto crc32_table = []() -> std::array<std::uint32_t, crc32_table_size> {
  std::array<std::uint32_t, crc32_table_size> table{};
  for (std::size_t index = 0; index < table.size(); ++index) {
    auto value = static_cast<std::uint32_t>(index);
    for (unsigned bit = 0; bit < bits_per_byte; ++bit) {
      value = (value >> 1U) ^ ((value & 1U) != 0 ? crc32_polynomial : 0U);
    }
    table.at(index) = value;
  }
  return table;
}();
[[nodiscard]] inline auto crc32(std::span<std::byte const> bytes) -> std::uint32_t
{
  auto value = crc32_mask;
  for (auto byte : bytes) {
    auto const index = static_cast<std::uint8_t>(value ^ std::to_integer<std::uint8_t>(byte));
    value = (value >> bits_per_byte) ^ crc32_table.at(index);
  }
  return value ^ crc32_mask;
}
}// namespace dacoexec::detail
#endif// DACOEXEC_DETAIL_CRC32_HPP
