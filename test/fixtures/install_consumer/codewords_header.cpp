#include <dacoexec/huffman/codewords_view.hpp>

static_assert(std::ranges::input_range<dacoexec::huffman::codewords_view<std::ranges::empty_view<std::byte>>>);
