#pragma once

#include <dacoexec/huffman/tree.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace dacoexec {
namespace huffman {

  inline constexpr unsigned max_canonical_bits = std::numeric_limits<std::uint32_t>::digits;

  // Low length_ bits, emitted most-significant-first. Zero length means absent.
  struct codeword
  {
    std::uint32_t bits_ = 0;
    std::uint8_t length_ = 0;

    friend constexpr auto operator==(codeword const &, codeword const &) -> bool = default;
  };

  class canonical_codebook;
  [[nodiscard]] constexpr auto try_make_canonical_codebook(code_length_table const &lengths)
    -> std::optional<canonical_codebook>;

  // Immutable value reconstructed solely from validated code lengths.
  class canonical_codebook
  {
  public:
    [[nodiscard]] constexpr auto operator[](std::uint8_t symbol) const noexcept -> codeword
    { return words_[symbol]; }

    [[nodiscard]] constexpr auto lengths() const noexcept -> code_length_table const & { return lengths_; }

  private:
    friend constexpr auto try_make_canonical_codebook(code_length_table const &lengths)
      -> std::optional<canonical_codebook>;

    constexpr canonical_codebook() = default;

    code_length_table lengths_{};
    std::array<codeword, alphabet_size> words_{};
  };

  // Unsupported widths return nullopt before structural validation. Supported
  // tables must be empty, a length-one singleton, or a complete prefix tree.
  // Invalid supported tables throw invalid_argument. No allocation is needed.
  [[nodiscard]] constexpr auto try_make_canonical_codebook(code_length_table const &lengths)
    -> std::optional<canonical_codebook>
  {
    std::array<std::uint32_t, max_canonical_bits + 1U> counts{};
    std::size_t present = 0;
    unsigned maximum = 0;
    for (auto const length : lengths) {
      if (length > max_canonical_bits) { return std::nullopt; }
      if (length == 0) { continue; }
      ++counts.at(length);
      ++present;
      if (length > maximum) { maximum = length; }
    }

    if (present == 1) {
      if (maximum != 1) { throw std::invalid_argument("Huffman singleton must have length one"); }
    } else if (present > 1) {
      std::uint64_t slots = 1;
      for (unsigned length = 1; length <= max_canonical_bits; ++length) {
        slots *= 2U;
        if (counts.at(length) > slots) { throw std::invalid_argument("Oversubscribed Huffman lengths"); }
        slots -= counts.at(length);
      }
      if (slots != 0) { throw std::invalid_argument("Incomplete Huffman lengths"); }
    }

    std::array<std::uint64_t, max_canonical_bits + 1U> next_code{};
    std::uint64_t code = 0;
    for (unsigned length = 1; length <= max_canonical_bits; ++length) {
      code = (code + counts.at(length - 1U)) << 1U;
      next_code.at(length) = code;
    }
    canonical_codebook result;
    result.lengths_ = lengths;
    for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
      auto const length = lengths.at(symbol);
      if (length == 0) { continue; }
      result.words_.at(symbol) = codeword{ static_cast<std::uint32_t>(next_code.at(length)), length };
      ++next_code.at(length);
    }
    return result;
  }

  // Same validation and assignment as try_make; unsupported widths throw length_error.
  [[nodiscard]] constexpr auto make_canonical_codebook(code_length_table const &lengths) -> canonical_codebook
  {
    auto result = try_make_canonical_codebook(lengths);
    if (!result) { throw std::length_error("Huffman code length exceeds codeword width"); }
    return *result;
  }

}// namespace huffman
}// namespace dacoexec
