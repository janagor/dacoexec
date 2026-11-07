//! \file
//! Bounded-count Huffman decoding and terminal payload validation.

#ifndef DACOEXEC_HUFFMAN_DECODE_VIEW_HPP
#define DACOEXEC_HUFFMAN_DECODE_VIEW_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/huffman/canonical.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::huffman {

/**
 * Malformed Huffman block metadata or payload, including output-bound violations.
 *
 * Diagnostics use the inherited std::runtime_error constructors.
 * @see decompress_block
 */
class decode_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

namespace detail {
  struct decode_limits
  {
    std::uint64_t valid_bits_;
    std::uint64_t expected_symbols_;
    bool infer_padding_;
  };

  inline constexpr auto decode_node_capacity = alphabet_size * 2U;
  inline constexpr auto missing_decode_node = std::numeric_limits<std::uint16_t>::max();

  struct decode_node
  {
    std::array<std::uint16_t, 2> children_{ missing_decode_node, missing_decode_node };
    std::optional<std::byte> symbol_;
  };

  struct decode_lookup
  {
    explicit decode_lookup(canonical_codebook const &book)
    {
      std::uint16_t used = 1;
      for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
        auto const word = book.at(static_cast<std::uint8_t>(symbol));
        std::uint16_t node = 0;
        for (unsigned remaining = word.length_; remaining != 0; --remaining) {
          auto const bit = (word.bits_ >> (remaining - 1U)) & 1U;
          auto &child = nodes_.at(node).children_.at(bit);
          if (child == missing_decode_node) {
            if (used == decode_node_capacity) { throw decode_error("Huffman trie capacity exceeded"); }
            child = used++;
          }
          node = child;
        }
        if (word.length_ != 0) { nodes_.at(node).symbol_ = static_cast<std::byte>(symbol); }
      }
    }

    std::array<decode_node, decode_node_capacity> nodes_{};
  };
}// namespace detail

/**
 * Single-pass decoding with one staged byte and a fixed-capacity trie.
 * Construction never reads the source. Iterator copies share a position;
 * moving the owner preserves that position. Any failure permanently ends it.
 * Exhausting iteration checks exact bit/symbol counts, zero padding and EOF.
 *
 * Accepts a byte_input_range of packed bytes and yields std::byte values in
 * MSB-first codeword order. begin() stages the first symbol; increment decodes
 * the next or validates the end. Stopping early skips final validation.
 * Explicit mode requires exactly the declared meaningful bits. Inferred mode
 * additionally accepts a final partial byte of fewer than eight zero bits.
 * Truncation, invalid trie transitions, count mismatches, nonzero padding and
 * trailing bytes throw decode_error. Source exceptions propagate unchanged.
 *
 * The source view, fixed trie and cursor belong to one heap state. Iterators
 * survive moves of a started view; destroying its owner invalidates them.
 * Move assignment also invalidates iterators to the destination's old state.
 * Repeated begin() resumes the pass; default and moved-from views are empty.
 * Borrowed source storage must outlive iteration.
 *
 * @see views::decode
 */
template<std::ranges::view V>
  requires byte_input_range<V>
class decode_view : public std::ranges::view_interface<decode_view<V>>
{
  struct state
  {
    explicit state(V source, canonical_codebook codebook, detail::decode_limits const &limits)
      : base_(std::move(source)), lookup_(codebook), valid_bits_(limits.valid_bits_),
        expected_symbols_(limits.expected_symbols_), infer_padding_(limits.infer_padding_)
    {}

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        cursor_.emplace(std::ranges::begin(base_));
        load_next_symbol();
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto read_bit() -> unsigned
    {
      if (!cursor_.has_value()) { throw decode_error("Huffman source cursor is not initialized"); }
      auto &position = cursor_.value();
      if (consumed_bits_ == valid_bits_) { throw decode_error("Truncated Huffman codeword"); }
      if (cached_bits_ == 0) {
        if (position == std::ranges::end(base_)) { throw decode_error("Truncated Huffman payload"); }
        using source_value = std::ranges::range_value_t<V>;
        cache_ = std::to_integer<std::uint8_t>(dacoexec::detail::to_byte(static_cast<source_value>(*position)));
        cached_bits_ = dacoexec::detail::bits_per_byte;
      }
      --cached_bits_;
      ++consumed_bits_;
      auto const bit = (static_cast<unsigned>(cache_) >> cached_bits_) & 1U;
      if (cached_bits_ == 0) { std::ranges::advance(position, 1); }
      return bit;
    }

    auto validate_end() -> void
    {
      if (!cursor_.has_value()) { throw decode_error("Huffman source cursor is not initialized"); }
      auto &position = cursor_.value();
      if (consumed_bits_ != valid_bits_
          && (!infer_padding_ || valid_bits_ - consumed_bits_ >= dacoexec::detail::bits_per_byte
              || valid_bits_ - consumed_bits_ != cached_bits_)) {
        throw decode_error("Trailing meaningful Huffman bits");
      }
      if (cached_bits_ != 0) {
        auto const mask = (1U << cached_bits_) - 1U;
        if ((static_cast<unsigned>(cache_) & mask) != 0) { throw decode_error("Nonzero Huffman padding"); }
        std::ranges::advance(position, 1);
      }
      if (position != std::ranges::end(base_)) { throw decode_error("Trailing Huffman payload bytes"); }
      finished_ = true;
    }

    auto load_next_symbol() -> void
    {
      try {
        if (produced_symbols_ == expected_symbols_) {
          validate_end();
          return;
        }
        std::uint16_t node = 0;
        while (true) {
          auto const &symbol = lookup_.nodes_.at(node).symbol_;
          if (symbol.has_value()) {
            current_ = symbol.value();
            ++produced_symbols_;
            return;
          }
          node = lookup_.nodes_.at(node).children_.at(read_bit());
          if (node == detail::missing_decode_node) { throw decode_error("Invalid Huffman trie transition"); }
        }
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto advance() -> void
    {
      if (!finished_) { load_next_symbol(); }
    }

    // base is declared before cursor so the source iterator is destroyed first.
    V base_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    detail::decode_lookup lookup_;
    std::uint64_t valid_bits_ = 0;
    std::uint64_t expected_symbols_ = 0;
    std::uint64_t consumed_bits_ = 0;
    std::uint64_t produced_symbols_ = 0;
    std::uint8_t cache_ = 0;
    unsigned cached_bits_ = 0;
    std::byte current_{};
    bool infer_padding_ = false;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  decode_view() = default;

  /**
   * Stores the source and constructs its decoding trie without reading input.
   *
   * @param bits Meaningful bit count, or full payload bits for inferred padding.
   * @param symbols Exact decoded byte count.
   * @param infer_padding Allow a final partial byte of zero padding.
   * @see views::decode
   */
  explicit decode_view(V base,
    canonical_codebook codebook,
    std::uint64_t bits,
    std::uint64_t symbols,
    bool infer_padding = false)
    : state_(std::make_unique<state>(std::move(base),
        codebook,
        detail::decode_limits{ .valid_bits_ = bits, .expected_symbols_ = symbols, .infer_padding_ = infer_padding }))
  {}

  decode_view(decode_view const &) = delete;
  auto operator=(decode_view const &) -> decode_view & = delete;

  decode_view(decode_view &&) noexcept = default;

  auto operator=(decode_view &&other) noexcept -> decode_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~decode_view() = default;

  /**
   * Single-pass cursor sharing the view state; copies do not provide independent passes.
   *
   * @pre Dereference requires a live owning state and a non-end position.
   */
  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;
    using reference = std::byte;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> std::byte { return state_->current_; }

    auto operator++() -> iterator &
    {
      state_->advance();
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor,
      [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.state_ == nullptr || cursor.state_->finished_; }

  private:
    friend class decode_view;

    explicit iterator(state *state) : state_(state) {}

    state *state_ = nullptr;
  };

  /**
   * Starts lazy consumption on first use, then returns the current shared position.
   *
   * Source exceptions propagate according to the class contract.
   */
  [[nodiscard]] auto begin() -> iterator
  {
    if (state_ == nullptr) { return iterator{ nullptr }; }
    state_->ensure_initialized();
    return iterator{ state_.get() };
  }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::unique_ptr<state> state_;
};

namespace views {
  namespace detail {

    class decode_closure
    {
    public:
      explicit decode_closure(canonical_codebook codebook, huffman::detail::decode_limits const &limits)
        : codebook_(codebook), limits_(limits)
      {}

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const -> decode_view<std::views::all_t<R>>
      {
        return decode_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)),
          codebook_,
          limits_.valid_bits_,
          limits_.expected_symbols_,
          limits_.infer_padding_);
      }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, decode_closure const &adaptor) -> decode_view<std::views::all_t<R>>
      { return adaptor(std::forward<R>(range)); }

    private:
      canonical_codebook codebook_;
      huffman::detail::decode_limits limits_;
    };

  }// namespace detail

  /**
   * Creates a lazy MSB-first byte-payload decoder with an owned codebook copy.
   *
   * @param codebook Validated canonical mapping.
   * @param valid_bit_count Available meaningful bits, or the full payload bit count
   *     when inferring padding.
   * @param expected_symbol_count Exact number of bytes to produce.
   * @param infer_padding Permit fewer than eight unused zero bits in the final
   *     byte after the expected symbols; otherwise require exact meaningful bits.
   * Malformed content throws decode_error during iteration. Exhaust the view to
   * validate trailing bits, zero padding, and source exhaustion.
   *
   * @code
   * auto decoded = payload | dacoexec::huffman::views::decode(book, bits, symbols);
   * @endcode
   *
   * @see decode_view
   */
  [[nodiscard]] inline auto decode(canonical_codebook codebook,
    std::uint64_t valid_bit_count,
    std::uint64_t expected_symbol_count,
    bool infer_padding = false) -> detail::decode_closure
  {
    return detail::decode_closure{ codebook,
      huffman::detail::decode_limits{
        .valid_bits_ = valid_bit_count, .expected_symbols_ = expected_symbol_count, .infer_padding_ = infer_padding } };
  }

}// namespace views
}// namespace dacoexec::huffman

#endif// DACOEXEC_HUFFMAN_DECODE_VIEW_HPP
