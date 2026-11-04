#ifndef DACOEXEC_HUFFMAN_TREE_HPP
#define DACOEXEC_HUFFMAN_TREE_HPP

#include <dacoexec/huffman/frequency.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace dacoexec::huffman {

  // Code length of every byte value. Zero means the symbol is absent.
  // A present symbol has a length of at least 1. Lengths are leaf depths and
  // are stored in full: a 256-symbol tree can reach depth 255, which still
  // fits in std::uint8_t.
  using code_length_table = std::array<std::uint8_t, alphabet_size>;

  // Build one deterministic Huffman length for every byte value.
  //
  // Equal frequencies are ordered by the smallest symbol in the subtree, then
  // by the earlier node. The result does not depend on pointer values,
  // allocation, or the order in which the table was filled. The same table
  // produces the same lengths on every supported compiler.
  //
  // An empty table yields all zeros. One present symbol receives length 1, so
  // zero stays reserved for absent symbols. Two present symbols both receive
  // length 1. A parent frequency that would pass the maximum std::uint64_t
  // throws std::overflow_error.
  //
  // Construction uses a fixed array of 511 nodes and a fixed heap. It performs
  // no dynamic allocation.
  [[nodiscard]] constexpr auto build_code_lengths(frequency_table const &frequencies) -> code_length_table;

  namespace detail {

    // 256 leaves plus at most 255 internal nodes.
    inline constexpr std::size_t max_tree_nodes = (alphabet_size * 2U) - 1U;

    // Not a valid node index. A leaf uses this value for both children.
    inline constexpr std::uint16_t invalid_link = std::numeric_limits<std::uint16_t>::max();

    inline constexpr std::size_t expected_tree_nodes = 511U;
    static_assert(max_tree_nodes == expected_tree_nodes);
    static_assert(max_tree_nodes < invalid_link);
    static_assert(alphabet_size - 1U <= std::numeric_limits<std::uint8_t>::max());

    // Temporary node used only while deriving code lengths.
    // left_ and right_ are indexes into the owning node array.
    struct tree_node
    {
      std::uint64_t frequency_ = 0;
      std::uint16_t min_symbol_ = 0;
      std::uint16_t left_ = invalid_link;
      std::uint16_t right_ = invalid_link;
      std::uint16_t symbol_ = invalid_link;

      [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool
      { return left_ == invalid_link && right_ == invalid_link; }
    };

    // Min-heap key. Lower frequency, then lower min_symbol, then lower node_index.
    struct heap_entry
    {
      std::uint64_t frequency_ = 0;
      std::uint16_t min_symbol_ = 0;
      std::uint16_t node_index_ = 0;
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
    // allocation-free construction; the forest never exceeds alphabet_size entries.
    class tree_priority_queue
    {
    public:
      [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return size_; }

      constexpr auto push(heap_entry const &entry) -> void
      {
        entries_.at(size_) = entry;
        ++size_;
        std::push_heap(entries_.begin(), std::next(entries_.begin(), static_cast<std::ptrdiff_t>(size_)), heap_order{});
      }

      [[nodiscard]] constexpr auto pop() -> heap_entry
      {
        std::pop_heap(entries_.begin(), std::next(entries_.begin(), static_cast<std::ptrdiff_t>(size_)), heap_order{});
        --size_;
        return entries_.at(size_);
      }

    private:
      std::array<heap_entry, alphabet_size> entries_{};
      std::size_t size_ = 0;
    };

    // Owns the Huffman forest and all state for one length computation.
    class length_builder
    {
    public:
      [[nodiscard]] constexpr auto build(frequency_table const &frequencies) -> code_length_table
      {
        for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
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
        nodes_.at(index) = tree_node{
          .frequency_ = frequency,
          .min_symbol_ = symbol,
          .left_ = invalid_link,
          .right_ = invalid_link,
          .symbol_ = symbol,
        };
        ++node_count_;
        queue_.push(heap_entry{ .frequency_ = frequency, .min_symbol_ = symbol, .node_index_ = index });
      }

      constexpr auto merge_pair() -> void
      {
        heap_entry const first = queue_.pop();
        heap_entry const second = queue_.pop();
        std::uint64_t const left_frequency = nodes_.at(first.node_index_).frequency_;
        std::uint64_t const right_frequency = nodes_.at(second.node_index_).frequency_;
        if (left_frequency > std::numeric_limits<std::uint64_t>::max() - right_frequency) {
          throw std::overflow_error("Huffman tree frequency overflow");
        }
        std::uint64_t const frequency = left_frequency + right_frequency;
        std::uint16_t const min_symbol =
          std::min(nodes_.at(first.node_index_).min_symbol_, nodes_.at(second.node_index_).min_symbol_);
        auto const index = node_count_;
        // The first popped node is the left child and the second is the right child.
        nodes_.at(index) = tree_node{
          .frequency_ = frequency,
          .min_symbol_ = min_symbol,
          .left_ = first.node_index_,
          .right_ = second.node_index_,
          .symbol_ = invalid_link,
        };
        ++node_count_;
        queue_.push(heap_entry{ .frequency_ = frequency, .min_symbol_ = min_symbol, .node_index_ = index });
      }

      [[nodiscard]] constexpr auto depths_from(std::uint16_t root) const -> code_length_table
      {
        struct frame
        {
          std::uint16_t index_ = 0;
          std::uint16_t depth_ = 0;
        };

        code_length_table lengths{};
        std::array<frame, max_tree_nodes> stack{};
        std::size_t top = 1;
        stack.at(0) = frame{ .index_ = root, .depth_ = 0 };
        while (top != 0) {
          frame const current = stack.at(top - 1);
          --top;
          tree_node const node = nodes_.at(current.index_);
          if (node.is_leaf()) {
            lengths.at(node.symbol_) = static_cast<std::uint8_t>(current.depth_);
            continue;
          }
          auto const child_depth = static_cast<std::uint16_t>(current.depth_ + 1U);
          stack.at(top) = frame{ .index_ = node.right_, .depth_ = child_depth };
          ++top;
          stack.at(top) = frame{ .index_ = node.left_, .depth_ = child_depth };
          ++top;
        }
        return lengths;
      }

      [[nodiscard]] constexpr auto finish() -> code_length_table
      {
        if (node_count_ == 0) { return {}; }
        if (node_count_ == 1) {
          code_length_table lengths{};
          lengths.at(nodes_.at(0).symbol_) = std::uint8_t{ 1 };
          return lengths;
        }
        while (queue_.size() > 1) { merge_pair(); }
        return depths_from(static_cast<std::uint16_t>(node_count_ - 1));
      }

      std::array<tree_node, max_tree_nodes> nodes_{};
      tree_priority_queue queue_{};
      std::uint16_t node_count_ = 0;
    };

  }// namespace detail

  [[nodiscard]] constexpr auto build_code_lengths(frequency_table const &frequencies) -> code_length_table
  {
    return detail::length_builder{}.build(frequencies);
  }

}// namespace dacoexec::huffman

#endif // DACOEXEC_HUFFMAN_TREE_HPP
