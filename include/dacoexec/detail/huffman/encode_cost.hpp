//! \file
#ifndef DACOEXEC_HUFFMAN_DETAIL_ENCODE_COST_HPP
#define DACOEXEC_HUFFMAN_DETAIL_ENCODE_COST_HPP

#include <dacoexec/detail/huffman/types.hpp>
#include <optional>
#include <ranges>

namespace dacoexec::detail::huffman {

// Unsupported widths and unrepresentable bit totals select RAW before
// allocating an encoded buffer. Lengths come from build_code_lengths;
// canonical validation and assignment happen once, only if Huffman wins.
[[nodiscard]] inline auto encoded_bit_count(model::frequency_table const &frequencies, code_length_table const &lengths)
  -> std::optional<std::uint64_t>
{
  std::uint64_t total = 0;
  for (auto const symbol : std::views::iota(std::size_t{ 0 }, model::alphabet_size)) {
    auto const frequency = frequencies.at(symbol);
    auto const length = lengths.at(symbol);
    if (length > max_canonical_bits) { return std::nullopt; }
    if (length == 0) { continue; }

    auto const remaining_bits = std::numeric_limits<std::uint64_t>::max() - total;
    if (frequency > remaining_bits / length) { return std::nullopt; }
    total += frequency * length;
  }
  return total;
}


}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_DETAIL_ENCODE_COST_HPP
