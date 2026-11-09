#include <dacoexec/detail/huffman/types.hpp>
#ifndef DACOEXEC_TEST_HUFFMAN_TEST_UTILS_HPP
#define DACOEXEC_TEST_HUFFMAN_TEST_UTILS_HPP

#include <dacoexec/detail/huffman/canonical.hpp>

#include <cstdint>

namespace test_detail {

// The public index type covers exactly the codebook's complete byte alphabet.
[[nodiscard]] constexpr auto canonical_word(dacoexec::detail::huffman::canonical_codebook const &book,
  std::uint8_t symbol) -> dacoexec::detail::huffman::codeword
{ return book.at(symbol); }

}// namespace test_detail

#endif// DACOEXEC_TEST_HUFFMAN_TEST_UTILS_HPP
