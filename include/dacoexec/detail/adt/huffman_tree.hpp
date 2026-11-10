//! \file
//! Node structure and typed storage for a byte-alphabet Huffman tree.
#ifndef DACOEXEC_ADT_HUFFMAN_TREE_HPP
#define DACOEXEC_ADT_HUFFMAN_TREE_HPP

#include <dacoexec/detail/memory/node_storage.hpp>
#include <dacoexec/detail/model/frequency.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace dacoexec::detail::adt {

// 256 leaves plus at most 255 internal nodes.
inline constexpr std::size_t max_huffman_tree_nodes = (model::alphabet_size * 2U) - 1U;

// An internal node has no symbol.
inline constexpr std::uint16_t invalid_huffman_symbol = std::numeric_limits<std::uint16_t>::max();

inline constexpr std::size_t expected_huffman_tree_nodes = 511U;
static_assert(max_huffman_tree_nodes == expected_huffman_tree_nodes);
static_assert(max_huffman_tree_nodes < invalid_huffman_symbol);
static_assert(model::alphabet_size - 1U <= std::numeric_limits<std::uint8_t>::max());

// Temporary node used only while deriving code lengths.
struct huffman_tree_node
{
  std::uint64_t frequency_ = 0;
  std::uint16_t min_symbol_ = 0;
  huffman_tree_node const *left_ = nullptr;
  huffman_tree_node const *right_ = nullptr;
  std::uint16_t symbol_ = invalid_huffman_symbol;

  [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool { return left_ == nullptr && right_ == nullptr; }
};

/** Fixed-capacity typed storage for one complete Huffman tree. */
using static_huffman_tree_arena = memory::static_node_arena<huffman_tree_node, max_huffman_tree_nodes>;

/** Dynamically owned nodes; a runtime allocator such as PMR can be supplied. */
template<class Allocator = std::allocator<huffman_tree_node>>
using dynamic_huffman_tree_allocator = memory::dynamic_node_allocator<huffman_tree_node, Allocator>;

}// namespace dacoexec::detail::adt
#endif// DACOEXEC_ADT_HUFFMAN_TREE_HPP
