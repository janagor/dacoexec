//! \file
//! Deterministic, allocation-free Huffman code-length construction.

#ifndef DACOEXEC_HUFFMAN_TREE_HPP
#define DACOEXEC_HUFFMAN_TREE_HPP

#include <dacoexec/detail/huffman/frequency.hpp>
#include <dacoexec/detail/huffman/node_storage.hpp>
#include <dacoexec/detail/huffman/tree_builder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>

namespace dacoexec::detail::huffman {

/**
 * Build one deterministic Huffman length for every byte value.
 *
 * Equal frequencies are ordered by the smallest symbol in the subtree, then
 * by the earlier node. The result does not depend on pointer values,
 * allocation, or the order in which the table was filled. The same table
 * produces the same lengths on every supported compiler.
 *
 * An empty table yields all zeros. One present symbol receives length 1, so
 * zero stays reserved for absent symbols. Two present symbols both receive
 * length 1. A parent frequency that would pass the maximum std::uint64_t
 * throws std::overflow_error.
 *
 * Construction uses a fixed array of 511 nodes and a fixed heap. It performs
 * no dynamic allocation.
 *
 * @param frequencies Occurrence counts; zeros denote absent symbols.
 * @return Per-symbol depths, including zeros for absent symbols.
 * @throws std::overflow_error If a merged frequency exceeds std::uint64_t.
 * @see make_canonical_codebook
 */
[[nodiscard]] constexpr auto build_code_lengths(frequency_table const &frequencies) -> code_length_table;


/** Fixed-capacity typed storage for one complete Huffman tree. */
using static_tree_arena =
  static_node_arena<::dacoexec::detail::huffman::tree_node, ::dacoexec::detail::huffman::max_tree_nodes>;

/** Dynamically owned nodes; a runtime allocator such as PMR can be supplied. */
template<class Allocator = std::allocator<::dacoexec::detail::huffman::tree_node>>
using dynamic_tree_allocator = dynamic_node_allocator<::dacoexec::detail::huffman::tree_node, Allocator>;

/**
 * Build with caller-owned typed storage. Existing nodes remain alive; new nodes
 * are appended. Storage must keep node addresses stable until this call returns.
 * Allocation failures propagate, and storage retains nodes already created.
 */
template<class NodeStorage>
[[nodiscard]] constexpr auto build_code_lengths(frequency_table const &frequencies, NodeStorage &storage)
  -> code_length_table
{ return ::dacoexec::detail::huffman::length_builder<NodeStorage>{ storage }.build(frequencies); }

[[nodiscard]] constexpr auto build_code_lengths(frequency_table const &frequencies) -> code_length_table
{
  static_tree_arena storage;
  return build_code_lengths(frequencies, storage);
}

}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_TREE_HPP
