//! \file
//! Public entry point for dependency-free byte views, bit I/O, Huffman coding, and integer coding.
//!
//! Header-only C++20 library. Optional sender-based parallel APIs are provided
//! separately by huffman/execution.hpp and require stdexec.

#ifndef DACOEXEC_DACOEXEC_HPP
#define DACOEXEC_DACOEXEC_HPP

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/bit/bit_reader.hpp>
#include <dacoexec/concepts.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/canonical.hpp>
#include <dacoexec/huffman/codewords_view.hpp>
#include <dacoexec/huffman/decode_view.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/stream.hpp>
#include <dacoexec/huffman/tree.hpp>
#include <dacoexec/integer_coding/delta.hpp>
#include <dacoexec/integer_coding/gamma.hpp>
#include <dacoexec/integer_coding/golomb.hpp>
#include <dacoexec/integer_coding/rice.hpp>
#include <dacoexec/integer_coding/sequence.hpp>
#include <dacoexec/io/output_sink.hpp>
#include <dacoexec/numeric/mapping.hpp>
#include <dacoexec/views/bits.hpp>
#include <dacoexec/views/blocks.hpp>
#include <dacoexec/views/buffered_bytes.hpp>
#include <dacoexec/views/frames.hpp>


#endif// DACOEXEC_DACOEXEC_HPP
