#pragma once

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/concepts.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/tree.hpp>
#include <dacoexec/views/bits.hpp>
#include <dacoexec/views/blocks.hpp>
#include <dacoexec/views/buffered_bytes.hpp>

// dacoexec is a header-only C++20 library.
// Public entry point for the byte and bit adaptors and Huffman code lengths.
// The core target stays free of third-party dependencies.
