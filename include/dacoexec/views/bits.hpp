//! \file
//! Lazy MSB-first byte-to-bit adaptation.

#ifndef DACOEXEC_VIEWS_BITS_HPP
#define DACOEXEC_VIEWS_BITS_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>

namespace dacoexec {
namespace detail {

  // Bit at `index` of `byte`, with index 0 as the most significant bit.
  // `index` must be less than bits_per_byte.
  [[nodiscard]] constexpr auto msb_first_bit(std::uint8_t byte, unsigned index) noexcept -> bool
  { return ((static_cast<unsigned>(byte) >> ((bits_per_byte - 1U) - index)) & 1U) != 0U; }

}// namespace detail

/**
 * Lazy view of the bits in a byte input range.
 *
 * Each byte becomes eight bool values, bit 7 first and bit 0 last. The order
 * does not depend on the host endianness. An empty byte range yields no bits.
 * The view never allocates a bit buffer. It caches one source byte, so the
 * underlying iterator is dereferenced once per byte rather than once per bit.
 *
 * Construction reads nothing. begin() reads the first byte. Advancing past
 * bit 0 reads the next byte, or reaches the end when the source is exhausted.
 *
 * The source and the one-byte cache live in one heap state object. Moving the
 * view transfers that object, so a view that has already started keeps its
 * place in the input. Iterators refer to the state and remain usable after
 * the view is moved. Destroying the view that owns the state invalidates
 * them. A moved-from view is empty.
 *
 * The range is single-pass. Copying an iterator aliases the same bit
 * position; it does not restart the source. Repeated begin() calls resume the
 * in-progress pass. An exception from the source propagates unchanged. No bit
 * is invented for the failed read, and later iteration does not resume.
 *
 * The stored source view may borrow external storage; that storage must remain
 * valid during iteration. Move assignment invalidates iterators into the
 * destination's replaced state. A default-constructed view is empty.
 */
template<std::ranges::view V>
  requires byte_input_range<V>
class bits_view : public std::ranges::view_interface<bits_view<V>>
{
  struct state
  {
    explicit state(V source) : base_(std::move(source)) {}

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        cursor_.emplace(std::ranges::begin(base_));
      } catch (...) {
        finished_ = true;
        throw;
      }
      load_next_byte();
    }

    auto load_next_byte() -> void
    {
      if (finished_ || !cursor_.has_value()) {
        finished_ = true;
        return;
      }

      try {
        auto const &position = *cursor_;
        auto const last = std::ranges::end(base_);
        if (position == last) {
          finished_ = true;
          return;
        }
        // Materialize range_value_t first so a proxy reference is converted
        // as the declared byte type, not as the proxy type itself.
        using source_value = std::ranges::range_value_t<V>;
        std::byte const value = detail::to_byte(static_cast<source_value>(*position));
        current_byte_ = std::to_integer<std::uint8_t>(value);
        bit_index_ = 0;
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto advance() -> void
    {
      if (finished_ || !cursor_.has_value()) { return; }
      if (bit_index_ + 1U < detail::bits_per_byte) {
        ++bit_index_;
        return;
      }

      try {
        std::ranges::advance(cursor_.value(), 1);
      } catch (...) {
        finished_ = true;
        throw;
      }
      load_next_byte();
    }

    // base is declared before cursor so the source iterator is destroyed first.
    V base_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    std::uint8_t current_byte_ = 0;
    unsigned bit_index_ = 0;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  bits_view() = default;

  /**
   * Stores the source view without consuming it; state allocation may throw.
   */
  explicit bits_view(V base) : state_(std::make_unique<state>(std::move(base))) {}

  bits_view(bits_view const &) = delete;
  auto operator=(bits_view const &) -> bits_view & = delete;

  bits_view(bits_view &&) noexcept = default;

  auto operator=(bits_view &&other) noexcept -> bits_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~bits_view() = default;

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
    using value_type = bool;
    using difference_type = std::ptrdiff_t;
    using reference = bool;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> bool
    { return detail::msb_first_bit(state_->current_byte_, state_->bit_index_); }

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
    friend class bits_view;

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

    class bits_closure
    {
    public:
      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const -> bits_view<std::views::all_t<R>>
      { return bits_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range))); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, bits_closure const &adaptor) -> bits_view<std::views::all_t<R>>
      { return adaptor(std::forward<R>(range)); }
    };

  }// namespace detail

  /**
   * Creates a lazy byte-to-bool adaptor for a viewable byte_input_range.
   *
   * @code
   * auto bit_values = bytes | dacoexec::views::bits();
   * @endcode
   *
   * @see bits_view
   */
  [[nodiscard]] constexpr auto bits() noexcept -> detail::bits_closure { return detail::bits_closure{}; }

}// namespace views
}// namespace dacoexec

#endif// DACOEXEC_VIEWS_BITS_HPP
