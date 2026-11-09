//! \file
#ifndef DACOEXEC_HUFFMAN_DETAIL_DECODE_LOOKUP_HPP
#define DACOEXEC_HUFFMAN_DETAIL_DECODE_LOOKUP_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/huffman/canonical.hpp>
#include <dacoexec/detail/huffman/error.hpp>
#include <limits>
#include <optional>

namespace dacoexec::detail::huffman {


struct decode_limits
{
  std::uint64_t valid_bits_;
  std::uint64_t expected_symbols_;
  bool infer_padding_;
};

inline constexpr auto decode_node_capacity = alphabet_size * 2U;
inline constexpr auto missing_decode_node = std::numeric_limits<std::uint16_t>::max();

struct decode_node
{
  std::array<std::uint16_t, 2> children_{ missing_decode_node, missing_decode_node };
  std::optional<std::byte> symbol_;
};

struct decode_lookup
{
  explicit decode_lookup(canonical_codebook const &book)
  {
    std::uint16_t used = 1;
    for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
      auto const word = book.at(static_cast<std::uint8_t>(symbol));
      std::uint16_t node = 0;
      for (unsigned remaining = word.length_; remaining != 0; --remaining) {
        auto const bit = (word.bits_ >> (remaining - 1U)) & 1U;
        auto &child = nodes_.at(node).children_.at(bit);
        if (child == missing_decode_node) {
          if (used == decode_node_capacity) { throw decode_error("Huffman trie capacity exceeded"); }
          child = used++;
        }
        node = child;
      }
      if (word.length_ != 0) { nodes_.at(node).symbol_ = static_cast<std::byte>(symbol); }
    }
  }

  std::array<decode_node, decode_node_capacity> nodes_{};
};

}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_DETAIL_DECODE_LOOKUP_HPP
