//! \file
//! Huffman byte frequency counting with checked counters.

#ifndef DACOEXEC_HUFFMAN_FREQUENCY_HPP
#define DACOEXEC_HUFFMAN_FREQUENCY_HPP

#include <dacoexec/detail/codec/frequency.hpp>

namespace dacoexec::detail::huffman {

using codec::alphabet_size;
using codec::frequency_table;

template<byte_input_range R> constexpr auto accumulate_frequencies(frequency_table &frequencies, R &&input) -> void
{ codec::accumulate_frequencies(frequencies, std::forward<R>(input), "Huffman frequency counter overflow"); }

template<byte_input_range R> [[nodiscard]] constexpr auto count_frequencies(R &&input) -> frequency_table
{ return codec::count_frequencies(std::forward<R>(input), "Huffman frequency counter overflow"); }

}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_FREQUENCY_HPP
