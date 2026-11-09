//! \file
//! Byte alphabet rANS block coding. The payload starts with a little-endian
//! 32-bit decoder state followed by renormalization bytes in decode order.
#ifndef DACOEXEC_ANS_RANS_HPP
#define DACOEXEC_ANS_RANS_HPP
#include <dacoexec/detail/ans/state.hpp>
#include <limits>
#include <utility>

namespace dacoexec::detail::ans::rans {
template<byte_input_range R>
[[nodiscard]] auto encode_block(R &&source, frequency_model<alphabet_size> const &model) -> std::vector<std::byte>
{
  if constexpr (std::ranges::bidirectional_range<R>) {
    return ::dacoexec::detail::ans::encode(std::forward<R>(source), model);
  } else {
    // Single-pass sources need owned storage before reverse encoding.
    std::vector<std::byte> symbols;
    if constexpr (std::ranges::sized_range<R>) { symbols.reserve(std::ranges::size(source)); }
    for (auto value : source) {
      using source_value = std::ranges::range_value_t<R>;
      symbols.push_back(dacoexec::detail::support::to_byte(static_cast<source_value>(value)));
    }
    return ::dacoexec::detail::ans::encode(symbols, model);
  }
}

[[nodiscard]] inline auto decode_block(std::span<std::byte const> payload,
  frequency_model<alphabet_size> const &model,
  std::size_t count,
  std::size_t limit = std::numeric_limits<std::size_t>::max()) -> std::vector<std::byte>
{ return ::dacoexec::detail::ans::decode<std::byte>(payload, model, count, limit); }
}// namespace dacoexec::detail::ans::rans
#endif// DACOEXEC_ANS_RANS_HPP
