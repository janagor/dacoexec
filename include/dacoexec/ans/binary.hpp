//! \file
//! Binary-alphabet specialization of rANS (not a distinct ABS algorithm).
#ifndef DACOEXEC_ANS_BINARY_HPP
#define DACOEXEC_ANS_BINARY_HPP
#include <dacoexec/ans/detail/state.hpp>
#include <limits>
#include <utility>

namespace dacoexec::ans::binary {
template<std::ranges::input_range R>
[[nodiscard]] auto encode_block(R &&source, frequency_model<2> const &model) -> std::vector<std::byte>
{
  std::vector<std::uint8_t> symbols;
  for (auto value : std::forward<R>(source)) {
    if (value != 0 && value != 1) { throw std::invalid_argument("Invalid binary ANS symbol"); }
    symbols.push_back(static_cast<std::uint8_t>(value));
  }
  return detail::encode(std::span<std::uint8_t const>{ symbols }, model);
}

[[nodiscard]] inline auto decode_block(std::span<std::byte const> payload,
  frequency_model<2> const &model,
  std::size_t count,
  std::size_t limit = std::numeric_limits<std::size_t>::max()) -> std::vector<bool>
{ return detail::decode<bool>(payload, model, count, limit); }
}// namespace dacoexec::ans::binary
#endif// DACOEXEC_ANS_BINARY_HPP
