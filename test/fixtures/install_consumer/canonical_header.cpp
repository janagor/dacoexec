#include <dacoexec/huffman/canonical.hpp>

static_assert(dacoexec::huffman::make_canonical_codebook({})[0] == dacoexec::huffman::codeword{});
