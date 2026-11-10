//! \file
#ifndef DACOEXEC_HUFFMAN_DETAIL_TREE_BUILDER_HPP
#define DACOEXEC_HUFFMAN_DETAIL_TREE_BUILDER_HPP

#include <algorithm>
#include <dacoexec/detail/adt/static_priority_queue.hpp>
#include <dacoexec/detail/huffman/types.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <stdexcept>

namespace dacoexec::detail::huffman {


// 256 leaves plus at most 255 internal nodes.
inline constexpr std::size_t max_tree_nodes = (model::alphabet_size * 2U) - 1U;

// An internal node has no symbol.
inline constexpr std::uint16_t invalid_symbol = std::numeric_limits<std::uint16_t>::max();

inline constexpr std::size_t expected_tree_nodes = 511U;
static_assert(max_tree_nodes == expected_tree_nodes);
static_assert(max_tree_nodes < invalid_symbol);
static_assert(model::alphabet_size - 1U <= std::numeric_limits<std::uint8_t>::max());

// Temporary node used only while deriving code lengths.
struct tree_node
{
  std::uint64_t frequency_ = 0;
  std::uint16_t min_symbol_ = 0;
  tree_node const *left_ = nullptr;
  tree_node const *right_ = nullptr;
  std::uint16_t symbol_ = invalid_symbol;

  [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool { return left_ == nullptr && right_ == nullptr; }
};

// Min-heap key. Lower frequency, then lower min_symbol, then lower node_index.
struct heap_entry
{
  std::uint64_t frequency_ = 0;
  std::uint16_t min_symbol_ = 0;
  std::uint16_t node_index_ = 0;
  tree_node const *node_ = nullptr;
};

// Inverted comparison so the priority queue keeps the minimum key at the front.
struct heap_order
{
  [[nodiscard]] constexpr auto operator()(heap_entry const &lhs, heap_entry const &rhs) const noexcept -> bool
  {
    if (lhs.frequency_ != rhs.frequency_) { return lhs.frequency_ > rhs.frequency_; }
    if (lhs.min_symbol_ != rhs.min_symbol_) { return lhs.min_symbol_ > rhs.min_symbol_; }
    return lhs.node_index_ > rhs.node_index_;
  }
};

// C++20 std::priority_queue is not constexpr. Fixed storage also preserves
// allocation-free construction; the forest never exceeds model::alphabet_size entries.
using tree_priority_queue = adt::static_priority_queue<heap_entry, model::alphabet_size, heap_order>;

// Builds a Huffman forest in caller-owned storage for one length computation.
template<class NodeStorage> class length_builder
{
public:
  constexpr explicit length_builder(NodeStorage &nodes) noexcept : nodes_(&nodes) {}

  [[nodiscard]] constexpr auto build(model::frequency_table const &frequencies) -> code_length_table
  {
    for (std::size_t symbol = 0; symbol < model::alphabet_size; ++symbol) {
      std::uint64_t const frequency = frequencies.at(symbol);
      if (frequency == 0) { continue; }
      add_leaf(static_cast<std::uint16_t>(symbol), frequency);
    }
    return finish();
  }

private:
  constexpr auto add_leaf(std::uint16_t symbol, std::uint64_t frequency) -> void
  {
    auto const index = node_count_;
    auto const *node = nodes_->create(tree_node{
      .frequency_ = frequency,
      .min_symbol_ = symbol,
      .left_ = nullptr,
      .right_ = nullptr,
      .symbol_ = symbol,
    });
    ++node_count_;
    queue_.push(heap_entry{ .frequency_ = frequency, .min_symbol_ = symbol, .node_index_ = index, .node_ = node });
  }

  constexpr auto merge_pair() -> void
  {
    heap_entry const first = queue_.pop();
    heap_entry const second = queue_.pop();
    std::uint64_t const left_frequency = first.node_->frequency_;
    std::uint64_t const right_frequency = second.node_->frequency_;
    if (left_frequency > std::numeric_limits<std::uint64_t>::max() - right_frequency) {
      throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "Huffman tree frequency overflow");
    }
    std::uint64_t const frequency = left_frequency + right_frequency;
    std::uint16_t const min_symbol = std::min(first.node_->min_symbol_, second.node_->min_symbol_);
    auto const index = node_count_;
    // The first popped node is the left child and the second is the right child.
    auto const *node = nodes_->create(tree_node{
      .frequency_ = frequency,
      .min_symbol_ = min_symbol,
      .left_ = first.node_,
      .right_ = second.node_,
      .symbol_ = invalid_symbol,
    });
    ++node_count_;
    queue_.push(heap_entry{ .frequency_ = frequency, .min_symbol_ = min_symbol, .node_index_ = index, .node_ = node });
  }

  [[nodiscard]] static constexpr auto depths_from(tree_node const *root) -> code_length_table
  {
    struct frame
    {
      tree_node const *node_ = nullptr;
      std::uint16_t depth_ = 0;
    };

    code_length_table lengths{};
    std::array<frame, max_tree_nodes> stack{};
    std::size_t top = 1;
    stack.at(0) = frame{ .node_ = root, .depth_ = 0 };
    while (top != 0) {
      frame const current = stack.at(top - 1);
      --top;
      tree_node const &node = *current.node_;
      if (node.is_leaf()) {
        lengths.at(node.symbol_) = static_cast<std::uint8_t>(current.depth_);
        continue;
      }
      auto const child_depth = static_cast<std::uint16_t>(current.depth_ + 1U);
      stack.at(top) = frame{ .node_ = node.right_, .depth_ = child_depth };
      ++top;
      stack.at(top) = frame{ .node_ = node.left_, .depth_ = child_depth };
      ++top;
    }
    return lengths;
  }

  [[nodiscard]] constexpr auto finish() -> code_length_table
  {
    if (node_count_ == 0) { return {}; }
    if (node_count_ == 1) {
      code_length_table lengths{};
      lengths.at(queue_.pop().node_->symbol_) = std::uint8_t{ 1 };
      return lengths;
    }
    while (queue_.size() > 1) { merge_pair(); }
    return depths_from(queue_.pop().node_);
  }

  NodeStorage *nodes_;
  tree_priority_queue queue_{};
  std::uint16_t node_count_ = 0;
};


}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_DETAIL_TREE_BUILDER_HPP
