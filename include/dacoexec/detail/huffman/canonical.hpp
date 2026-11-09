//! \file
//! Validated canonical Huffman codebooks with bounded codeword widths.

#ifndef DACOEXEC_HUFFMAN_CANONICAL_HPP
#define DACOEXEC_HUFFMAN_CANONICAL_HPP

#include <dacoexec/detail/huffman/canonical_impl.hpp>
#include <dacoexec/detail/huffman/tree.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace dacoexec::detail::huffman {

class canonical_codebook;
[[nodiscard]] constexpr auto try_make_canonical_codebook(code_length_table const &lengths)
  -> std::optional<canonical_codebook>;

/**
 * Immutable value reconstructed solely from validated code lengths.
 * Codes are assigned by increasing length, then increasing numeric byte value.
 * Empty tables produce an empty book; a singleton uses the one-bit code zero.
 * No dynamic allocation is needed.
 *
 * @code
 * auto frequencies = huffman::count_frequencies(bytes);
 * auto lengths = huffman::build_code_lengths(frequencies);
 * auto book = huffman::make_canonical_codebook(lengths);
 * @endcode
 *
 * @see make_canonical_codebook
 * @see try_make_canonical_codebook
 */
class canonical_codebook
{
public:
  /**
   * Returns the symbol's codeword, with length zero when the symbol is absent.
   */
  [[nodiscard]] constexpr auto operator[](std::uint8_t symbol) const noexcept -> codeword { return at(symbol); }

  /**
   * Returns the symbol's codeword. Every uint8_t indexes the complete byte alphabet.
   */
  [[nodiscard]] constexpr auto at(std::uint8_t symbol) const noexcept -> codeword { return words_.at(symbol); }

  [[nodiscard]] constexpr auto lengths() const noexcept -> code_length_table const & { return lengths_; }

private:
  friend constexpr auto try_make_canonical_codebook(code_length_table const &lengths)
    -> std::optional<canonical_codebook>;

  constexpr canonical_codebook() = default;

  code_length_table lengths_{};
  std::array<codeword, alphabet_size> words_{};
};

/**
 * Unsupported widths return nullopt before structural validation. Supported
 * tables must be empty, a length-one singleton, or a complete prefix tree.
 * Invalid supported tables throw invalid_argument. No allocation is needed.
 *
 * @param lengths Width for each byte symbol; zero means absent.
 * @return Validated codebook, or std::nullopt if any width exceeds 32 bits.
 * @throws std::invalid_argument For supported-width tables with an invalid
 *     singleton, oversubscribed lengths, or an incomplete prefix tree.
 * @see make_canonical_codebook
 */
[[nodiscard]] constexpr auto try_make_canonical_codebook(code_length_table const &lengths)
  -> std::optional<canonical_codebook>
{
  auto const counts = ::dacoexec::detail::huffman::count_canonical_lengths(lengths);
  if (!counts) { return std::nullopt; }
  ::dacoexec::detail::huffman::validate_canonical_lengths(*counts);

  canonical_codebook result;
  result.lengths_ = lengths;
  result.words_ = ::dacoexec::detail::huffman::assign_canonical_codewords(lengths, *counts);
  return result;
}

/**
 * Constructs the canonical codebook with the same validation as try_make_canonical_codebook().
 *
 * @param lengths Width for each symbol; zero means absent.
 * @return Validated canonical codebook.
 * @throws std::length_error If any width exceeds max_canonical_bits.
 * @throws std::invalid_argument If a supported-width table is structurally invalid.
 * @see try_make_canonical_codebook
 */
[[nodiscard]] constexpr auto make_canonical_codebook(code_length_table const &lengths) -> canonical_codebook
{
  auto result = try_make_canonical_codebook(lengths);
  if (!result) { throw std::length_error("Huffman code length exceeds codeword width"); }
  return *result;
}

}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_CANONICAL_HPP
