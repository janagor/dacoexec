//! \file
#ifndef DACOEXEC_HUFFMAN_TYPES_HPP
#define DACOEXEC_HUFFMAN_TYPES_HPP

#include <array>
#include <cstdint>
#include <dacoexec/detail/huffman/frequency.hpp>
#include <limits>

namespace dacoexec::detail::huffman {
/**
 * Code length of every byte value. Zero means the symbol is absent.
 * A present symbol has a length of at least 1. Lengths are leaf depths and
 * are stored in full: a 256-symbol tree can reach depth 255, which still
 * fits in std::uint8_t.
 */
using code_length_table = std::array<std::uint8_t, alphabet_size>;

static_assert(alphabet_size == static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()) + 1U);

/**
 * Largest codeword width supported by canonical_codebook and bit_writer.
 */
inline constexpr unsigned max_canonical_bits = std::numeric_limits<std::uint32_t>::digits;

/**
 * Low length_ bits, emitted most-significant-first. Zero length means absent.
 */
struct codeword
{
  //! Code bits in the low length_ positions; emit highest of those bits first.
  std::uint32_t bits_ = 0;
  //! Meaningful width; zero denotes an absent symbol.
  std::uint8_t length_ = 0;

  friend constexpr auto operator==(codeword const &, codeword const &) -> bool = default;
};


}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_TYPES_HPP
