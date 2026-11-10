//! \file
//! Sizes of normalized probability model metadata in ANS archives.

#ifndef DACOEXEC_ANS_METADATA_HPP
#define DACOEXEC_ANS_METADATA_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/model/frequency_model.hpp>

namespace dacoexec::detail::ans {

inline constexpr std::size_t metadata_prefix_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t metadata_entry_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t min_metadata_size = metadata_prefix_size + metadata_entry_size;

[[nodiscard]] inline auto metadata_size(model::frequency_model<model::alphabet_size> const &model) -> std::size_t
{
  auto const present =
    std::ranges::count_if(model.frequencies(), [](auto frequency) -> bool { return frequency != 0; });
  return metadata_prefix_size + (static_cast<std::size_t>(present) * metadata_entry_size);
}

}// namespace dacoexec::detail::ans

#endif// DACOEXEC_ANS_METADATA_HPP
