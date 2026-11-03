#pragma once

#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>

namespace dacoexec {
namespace detail {

  // Value types accepted by byte_input_range. cv-qualifiers are stripped by the
  // concept before this check. References are not value types.
  template<class T>
  concept byte_element = std::same_as<T, std::byte> || std::same_as<T, char> || std::same_as<T, unsigned char>
                         || std::same_as<T, signed char> || std::same_as<T, char8_t>;

}// namespace detail

// Single-pass range whose elements are bytes.
//
// The source may be move-only. It does not have to be sized, common, forward,
// or contiguous. Character types are converted to std::byte when a consumer
// materializes them.
template<class R>
concept byte_input_range =
  std::ranges::input_range<R> && detail::byte_element<std::remove_cv_t<std::ranges::range_value_t<R>>>;

}// namespace dacoexec
