//! \file
//! Deterministic, allocation-free Huffman code-length construction.

#ifndef DACOEXEC_HUFFMAN_TREE_HPP
#define DACOEXEC_HUFFMAN_TREE_HPP

#include <dacoexec/detail/adt/huffman_tree.hpp>
#include <dacoexec/detail/huffman/tree_builder.hpp>
#include <dacoexec/detail/huffman/types.hpp>
#include <dacoexec/detail/model/frequency.hpp>

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
 * throws dacoexec::exception (errc::numeric_overflow).
 *
 * Construction uses a fixed array of 511 nodes and a fixed heap. It performs
 * no dynamic allocation.
 *
 * @param frequencies Occurrence counts; zeros denote absent symbols.
 * @return Per-symbol depths, including zeros for absent symbols.
 * @throws dacoexec::exception (errc::numeric_overflow) If a merged frequency exceeds std::uint64_t.
 * @see make_canonical_codebook
 */
[[nodiscard]] constexpr auto build_code_lengths(model::frequency_table const &frequencies) -> code_length_table;

/**
 * Build with caller-owned typed storage. Existing nodes remain alive; new nodes
 * are appended. Storage must keep node addresses stable until this call returns.
 * Allocation failures propagate, and storage retains nodes already created.
 */
template<class NodeStorage>
[[nodiscard]] constexpr auto build_code_lengths(model::frequency_table const &frequencies, NodeStorage &storage)
  -> code_length_table
{ return ::dacoexec::detail::huffman::length_builder<NodeStorage>{ storage }.build(frequencies); }

[[nodiscard]] constexpr auto build_code_lengths(model::frequency_table const &frequencies) -> code_length_table
{
  adt::static_huffman_tree_arena storage;
  return build_code_lengths(frequencies, storage);
}

}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_TREE_HPP
