//! \file
//! Byte input constraints shared by views and codecs.

#ifndef DACOEXEC_CONCEPTS_HPP
#define DACOEXEC_CONCEPTS_HPP

#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>

namespace dacoexec::detail {
namespace support {

  // Value types accepted by byte_input_range. cv-qualifiers are stripped by the
  // concept before this check. References are not value types.
  template<class T>
  concept byte_element = std::same_as<T, std::byte> || std::same_as<T, char> || std::same_as<T, unsigned char>
                         || std::same_as<T, signed char> || std::same_as<T, char8_t>;

}// namespace support

/**
 * Single-pass range of std::byte, char, signed char, unsigned char, or char8_t.
 * cv-qualified value types are accepted; wider character and integer types are not.
 *
 * The source may be move-only. It does not have to be sized, common, forward,
 * or contiguous. Character types are converted to std::byte when a consumer
 * materializes them.
 */
template<class R>
concept byte_input_range =
  std::ranges::input_range<R> && support::byte_element<std::remove_cv_t<std::ranges::range_value_t<R>>>;

}// namespace dacoexec::detail

#endif// DACOEXEC_CONCEPTS_HPP
