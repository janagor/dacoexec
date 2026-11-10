//! \file
//! Shared constrained transform closure for integer mapping adaptors.
#ifndef DACOEXEC_VIEWS_INTEGER_MAPPING_HPP
#define DACOEXEC_VIEWS_INTEGER_MAPPING_HPP

#include <concepts>
#include <limits>
#include <ranges>
#include <utility>

namespace dacoexec::detail::views {

// Validate the source value type before widening it. In particular, reject
// signed input to unsigned mappings instead of silently converting negatives.
template<typename Input, auto Mapping> struct integer_mapping_closure
{
  template<std::ranges::viewable_range R>
    requires std::ranges::input_range<R> && std::integral<std::ranges::range_value_t<R>>
             && (!std::same_as<std::ranges::range_value_t<R>, bool>)
             && (std::signed_integral<std::ranges::range_value_t<R>> == std::signed_integral<Input>)
             && (std::numeric_limits<std::ranges::range_value_t<R>>::digits <= std::numeric_limits<Input>::digits)
  [[nodiscard]] constexpr auto operator()(R &&range) const
  {
    using value_type = std::ranges::range_value_t<R>;
    return std::views::transform(
      std::forward<R>(range), [](value_type value) { return Mapping(static_cast<Input>(value)); });
  }

  template<std::ranges::viewable_range R>
    requires requires(integer_mapping_closure const &adaptor, R &&range) { adaptor(std::forward<R>(range)); }
  [[nodiscard]] friend constexpr auto operator|(R &&range, integer_mapping_closure const &adaptor)
  { return adaptor(std::forward<R>(range)); }
};

}// namespace dacoexec::detail::views
#endif// DACOEXEC_VIEWS_INTEGER_MAPPING_HPP
