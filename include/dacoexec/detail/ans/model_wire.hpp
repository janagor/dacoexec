//! \file
#ifndef DACOEXEC_ANS_DETAIL_FORMAT_HPP
#define DACOEXEC_ANS_DETAIL_FORMAT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/ans/model.hpp>

namespace dacoexec::detail::ans {
inline constexpr std::size_t model_prefix_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t model_entry_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t min_model_wire_size = model_prefix_size + model_entry_size;

[[nodiscard]] inline auto model_wire_size(frequency_model<alphabet_size> const &model) -> std::size_t
{
  auto const present =
    std::ranges::count_if(model.frequencies(), [](auto frequency) -> bool { return frequency != 0; });
  return model_prefix_size + (static_cast<std::size_t>(present) * model_entry_size);
}

}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_DETAIL_FORMAT_HPP
