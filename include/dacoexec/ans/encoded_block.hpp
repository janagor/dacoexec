//! \file
//! ANS encoded block support.
#ifndef DACOEXEC_ANS_ENCODED_BLOCK_HPP
#define DACOEXEC_ANS_ENCODED_BLOCK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/ans/rans.hpp>
#include <optional>
#include <vector>

namespace dacoexec::ans {

inline constexpr std::size_t model_prefix_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t model_entry_size = sizeof(std::uint8_t) + sizeof(std::uint16_t);
inline constexpr std::size_t min_model_wire_size = model_prefix_size + model_entry_size;

[[nodiscard]] inline auto model_wire_size(frequency_model<alphabet_size> const &model) -> std::size_t
{
  auto const present =
    std::ranges::count_if(model.frequencies(), [](auto frequency) -> bool { return frequency != 0; });
  return model_prefix_size + (static_cast<std::size_t>(present) * model_entry_size);
}
enum class block_encoding : std::uint8_t { raw, rans };

struct encoded_block
{
  std::uint64_t sequence_ = 0;
  block_encoding encoding_ = block_encoding::raw;
  std::uint64_t uncompressed_size_ = 0;
  std::vector<std::byte> payload_;
  std::optional<frequency_model<alphabet_size>> model_;
};

namespace detail {
  [[nodiscard]] inline auto model_metadata_size(encoded_block const &block) -> std::size_t
  {
    if (block.encoding_ == block_encoding::raw) { return 0; }
    if (!block.model_) { throw decode_error("Missing ANS model"); }
    return model_wire_size(*block.model_);
  }
}// namespace detail

}// namespace dacoexec::ans
#endif// DACOEXEC_ANS_ENCODED_BLOCK_HPP
