//! \file
//! Lazy DHF1 framing parser with owned frame payloads.

#ifndef DACOEXEC_VIEWS_FRAMES_HPP
#define DACOEXEC_VIEWS_FRAMES_HPP

#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/detail/views/huffman_frame_parser.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
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
#include <vector>

namespace dacoexec::detail {

/**
 * Lazy single-pass owning frame parser. Moving the view preserves cursor state.
 * Dereference transfers the staged value; increment invalidates its reference.
 * DATA frames receive consecutive sequence numbers starting at zero. After
 * sequence 2^64-1, another DATA frame throws overflow_error; END remains valid.
 * Any source or format failure permanently ends the pass without publishing it.
 *
 * Accepts a byte_input_range and yields move-only huffman::encoded_frame values.
 * Construction reads nothing. begin() validates the archive header and stages
 * the first complete data frame; increment reads the next frame. END is consumed
 * but not yielded. Missing END, trailing bytes, truncated input, invalid headers,
 * and invalid canonical tables throw format_error. Payload bits and CRC32 are
 * checked only by decoding, not by this parser. Parsed padding_bits_ is zero;
 * decoding must infer the final partial-byte padding.
 *
 * Iterator copies alias the staged frame; repeated begin() resumes that pass.
 * Dereferencing again after move-out observes the moved-from frame. Transferred
 * frames own their payload independently. Iterators survive moves of the view,
 * including after begin(); a moved-from or default view is empty. Destroying
 * the owner or replacing its state by move assignment invalidates its iterators.
 * The owned source view may itself borrow storage, which must remain valid.
 *
 * @throws std::invalid_argument If the configured limit is zero or exceeds
 *     huffman::max_frame_size.
 * @see views::frames
 */
template<std::ranges::view V>
  requires byte_input_range<V>
class frames_view : public std::ranges::view_interface<frames_view<V>>
{
  using state = support::huffman_frame_parser<V>;

public:
  frames_view() = default;

  /**
   * Stores the source view without reading it.
   *
   * @param frame_limit Maximum payload and uncompressed size of each data frame.
   * @throws std::invalid_argument If the limit is zero or above max_frame_size.
   */
  explicit frames_view(V base, std::size_t frame_limit)
  {
    if (frame_limit == 0 || frame_limit > huffman::max_frame_size) {
      throw std::invalid_argument("Invalid DHF1 frame limit");
    }
    state_ = std::make_unique<state>(std::move(base), frame_limit);
  }

  frames_view(frames_view const &) = delete;
  auto operator=(frames_view const &) -> frames_view & = delete;

  frames_view(frames_view &&) noexcept = default;

  auto operator=(frames_view &&other) noexcept -> frames_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~frames_view() = default;

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
    using value_type = huffman::encoded_frame;
    using difference_type = std::ptrdiff_t;
    using reference = huffman::encoded_frame &&;
    using pointer = void;

    /**
     * Returns the staged frame as an rvalue so range-for can move it out.
     * A second dereference without ++ observes the moved-from frame.
     * Dereferencing an end iterator is outside the input-iterator contract.
     */
    [[nodiscard]] auto operator*() const -> huffman::encoded_frame &&
    {
      if (!state_->current_) { throw std::bad_optional_access{}; }
      return std::move(*state_->current_);
    }

    auto operator++() -> iterator &
    {
      state_->load_next_frame();
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor,
      [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.state_ == nullptr || !cursor.state_->current_.has_value(); }

    [[nodiscard]] friend auto iter_move(iterator const &cursor) -> huffman::encoded_frame && { return *cursor; }

  private:
    friend class frames_view;

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


  class frames_closure
  {
  public:
    explicit constexpr frames_closure(std::size_t frame_limit) : frame_limit_(frame_limit)
    {
      if (frame_limit_ == 0 || frame_limit_ > huffman::max_frame_size) {
        throw std::invalid_argument("Invalid DHF1 frame limit");
      }
    }

    template<std::ranges::viewable_range R>
      requires byte_input_range<std::views::all_t<R>>
    [[nodiscard]] auto operator()(R &&range) const -> frames_view<std::views::all_t<R>>
    { return frames_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), frame_limit_); }

    template<std::ranges::viewable_range R>
      requires byte_input_range<std::views::all_t<R>>
    [[nodiscard]] friend auto operator|(R &&range, frames_closure const &adaptor) -> frames_view<std::views::all_t<R>>
    { return adaptor(std::forward<R>(range)); }

  private:
    std::size_t frame_limit_;
  };

  /**
   * Creates a lazy DHF1 parser for a viewable byte_input_range.
   *
   * @param frame_limit Bound on each data frame's payload and uncompressed sizes,
   *     excluding headers and the separate Huffman length table.
   * @throws std::invalid_argument If frame_limit is zero or exceeds max_frame_size.
   *
   * @code
   * auto frames = archive | dacoexec::detail::views::frames();
   * @endcode
   *
   * @see frames_view
   */
  [[nodiscard]] constexpr auto frames(std::size_t frame_limit = huffman::max_frame_size)
    -> ::dacoexec::detail::views::frames_closure
  { return ::dacoexec::detail::views::frames_closure{ frame_limit }; }

}// namespace views
}// namespace dacoexec::detail

#endif// DACOEXEC_VIEWS_FRAMES_HPP
