//! \file
//! Validated canonical Huffman codebooks with bounded codeword widths.

#ifndef DACOEXEC_HUFFMAN_CANONICAL_HPP
#define DACOEXEC_HUFFMAN_CANONICAL_HPP

#include <dacoexec/huffman/tree.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace dacoexec::huffman {

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

namespace detail {

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
      if (lengths.counts_.at(1) != 1) { throw std::invalid_argument("Huffman singleton must have length one"); }
      return;
    }

    std::uint64_t slots = 1;
    for (unsigned length = 1; length <= max_canonical_bits; ++length) {
      slots *= 2U;
      if (lengths.counts_.at(length) > slots) { throw std::invalid_argument("Oversubscribed Huffman lengths"); }
      slots -= lengths.counts_.at(length);
    }
    if (slots != 0) { throw std::invalid_argument("Incomplete Huffman lengths"); }
  }

  [[nodiscard]] constexpr auto assign_canonical_codewords(code_length_table const &lengths,
    canonical_length_counts const &counts) -> std::array<codeword, alphabet_size>
  {
    std::array<std::uint64_t, max_canonical_bits + 1U> next_code{};
    std::uint64_t code = 0;
    for (unsigned length = 1; length <= max_canonical_bits; ++length) {
      code = (code + counts.counts_.at(length - 1U)) << 1U;
      next_code.at(length) = code;
    }

    std::array<codeword, alphabet_size> words{};
    for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
      auto const length = lengths.at(symbol);
      if (length == 0) { continue; }
      words.at(symbol) = codeword{ .bits_ = static_cast<std::uint32_t>(next_code.at(length)), .length_ = length };
      ++next_code.at(length);
    }
    return words;
  }

}// namespace detail

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
  auto const counts = detail::count_canonical_lengths(lengths);
  if (!counts) { return std::nullopt; }
  detail::validate_canonical_lengths(*counts);

  canonical_codebook result;
  result.lengths_ = lengths;
  result.words_ = detail::assign_canonical_codewords(lengths, *counts);
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

}// namespace dacoexec::huffman

#endif// DACOEXEC_HUFFMAN_CANONICAL_HPP
