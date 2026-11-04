#ifndef DACOEXEC_TEST_HUFFMAN_TEST_UTILS_HPP
#define DACOEXEC_TEST_HUFFMAN_TEST_UTILS_HPP

#include <dacoexec/huffman/canonical.hpp>

#include <cstdint>

namespace test_detail {

// The public index type covers exactly the codebook's complete byte alphabet.
[[nodiscard]] constexpr auto canonical_word(dacoexec::huffman::canonical_codebook const &book, std::uint8_t symbol)
  -> dacoexec::huffman::codeword
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return book[symbol];
}

}// namespace test_detail

#endif// DACOEXEC_TEST_HUFFMAN_TEST_UTILS_HPP
