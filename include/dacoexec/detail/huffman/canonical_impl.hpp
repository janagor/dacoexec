//! \file
#ifndef DACOEXEC_HUFFMAN_DETAIL_CANONICAL_IMPL_HPP
#define DACOEXEC_HUFFMAN_DETAIL_CANONICAL_IMPL_HPP

#include <dacoexec/detail/huffman/types.hpp>
#include <dacoexec/error.hpp>
#include <optional>
#include <stdexcept>

namespace dacoexec::detail::huffman {


struct canonical_length_counts
{
  std::array<std::uint32_t, max_canonical_bits + 1U> counts_{};
  std::size_t present_ = 0;
};

[[nodiscard]] constexpr auto count_canonical_lengths(code_length_table const &lengths)
  -> std::optional<canonical_length_counts>
{
  canonical_length_counts result;
  for (auto const length : lengths) {
    if (length > max_canonical_bits) { return std::nullopt; }
    if (length == 0) { continue; }
    ++result.counts_.at(length);
    ++result.present_;
  }
  return result;
}

constexpr auto validate_canonical_lengths(canonical_length_counts const &lengths) -> void
{
  if (lengths.present_ == 0) { return; }
  if (lengths.present_ == 1) {
    if (lengths.counts_.at(1) != 1) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Huffman singleton must have length one");
    }
    return;
  }

  std::uint64_t slots = 1;
  for (unsigned length = 1; length <= max_canonical_bits; ++length) {
    slots *= 2U;
    if (lengths.counts_.at(length) > slots) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Oversubscribed Huffman lengths");
    }
    slots -= lengths.counts_.at(length);
  }
  if (slots != 0) { throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Incomplete Huffman lengths"); }
}

[[nodiscard]] constexpr auto assign_canonical_codewords(code_length_table const &lengths,
  canonical_length_counts const &counts) -> std::array<codeword, model::alphabet_size>
{
  std::array<std::uint64_t, max_canonical_bits + 1U> next_code{};
  std::uint64_t code = 0;
  for (unsigned length = 1; length <= max_canonical_bits; ++length) {
    code = (code + counts.counts_.at(length - 1U)) << 1U;
    next_code.at(length) = code;
  }

  std::array<codeword, model::alphabet_size> words{};
  for (std::size_t symbol = 0; symbol < model::alphabet_size; ++symbol) {
    auto const length = lengths.at(symbol);
    if (length == 0) { continue; }
    words.at(symbol) = codeword{ .bits_ = static_cast<std::uint32_t>(next_code.at(length)), .length_ = length };
    ++next_code.at(length);
  }
  return words;
}


}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_DETAIL_CANONICAL_IMPL_HPP
