#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace dacoexec {
namespace detail {

  // Bits produced for one source byte. Bit 7 is emitted first and bit 0 last.
  inline constexpr unsigned bits_per_byte = 8U;

  // Convert a byte_input_range element to std::byte.
  //
  // Signed character values go through unsigned char, so a value whose high
  // bit is set keeps that bit. The conversion is the low eight bits of the
  // value and does not depend on host endianness.
  template<class T> constexpr std::byte to_byte(T value) noexcept
  {
    using plain = std::remove_cv_t<T>;
    if constexpr (std::same_as<plain, std::byte>) {
      return value;
    } else if constexpr (std::same_as<plain, unsigned char>) {
      return static_cast<std::byte>(value);
    } else {
      static_assert(std::same_as<plain, char> || std::same_as<plain, signed char> || std::same_as<plain, char8_t>);
      return static_cast<std::byte>(static_cast<unsigned char>(value));
    }
  }

}// namespace detail
}// namespace dacoexec
