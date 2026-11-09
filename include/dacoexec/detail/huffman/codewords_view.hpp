//! \file
//! Lazy mapping of byte symbols to canonical codewords.

#ifndef DACOEXEC_HUFFMAN_CODEWORDS_VIEW_HPP
#define DACOEXEC_HUFFMAN_CODEWORDS_VIEW_HPP

#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/canonical.hpp>
#include <dacoexec/detail/support/byte.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::huffman {

/**
 * Single-pass lazy byte-to-codeword mapping with one staged value. Construction
 * reads nothing; begin stages the first symbol. Copies of iterators alias the
 * same position. The source and codebook live in one owned heap state, which
 * survives moves of the view. Destroying its owner invalidates iterators.
 * Source failures and absent symbols propagate and permanently end iteration.
 *
 * Accepts a byte_input_range and returns codeword values by value. Increment
 * advances one source symbol and stages its codeword; repeated begin() resumes
 * the current pass. An absent symbol throws std::invalid_argument. A moved-from
 * or default view is empty. Move assignment invalidates iterators to the old
 * destination state. The stored source view may borrow external storage.
 *
 * @see views::codewords
 */
template<std::ranges::view V>
  requires byte_input_range<V>
class codewords_view : public std::ranges::view_interface<codewords_view<V>>
{
  struct state
  {
    explicit state(V source, canonical_codebook codebook) : base_(std::move(source)), codebook_(codebook) {}

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
      load_next_word();
    }

    auto load_next_word() -> void
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
        std::byte const value = dacoexec::detail::support::to_byte(static_cast<source_value>(*position));
        current_ = codebook_.at(std::to_integer<std::uint8_t>(value));
        if (current_.length_ == 0) { throw std::invalid_argument("Symbol absent from Huffman codebook"); }
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto advance() -> void
    {
      if (finished_ || !cursor_.has_value()) { return; }
      try {
        std::ranges::advance(cursor_.value(), 1);
      } catch (...) {
        finished_ = true;
        throw;
      }
      load_next_word();
    }

    // base is declared before cursor so the source iterator is destroyed first.
    V base_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    canonical_codebook codebook_;
    codeword current_{};
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  codewords_view() = default;

  /**
   * Stores the source view and a codebook copy without reading the source.
   */
  explicit codewords_view(V base, canonical_codebook codebook)
    : state_(std::make_unique<state>(std::move(base), codebook))
  {}

  codewords_view(codewords_view const &) = delete;
  auto operator=(codewords_view const &) -> codewords_view & = delete;

  codewords_view(codewords_view &&) noexcept = default;

  auto operator=(codewords_view &&other) noexcept -> codewords_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~codewords_view() = default;

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
    using value_type = codeword;
    using difference_type = std::ptrdiff_t;
    using reference = codeword;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> codeword { return state_->current_; }

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
    friend class codewords_view;

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


  class codewords_closure
  {
  public:
    explicit codewords_closure(canonical_codebook codebook) : codebook_(codebook) {}

    template<std::ranges::viewable_range R>
      requires byte_input_range<std::views::all_t<R>>
    [[nodiscard]] auto operator()(R &&range) const -> codewords_view<std::views::all_t<R>>
    { return codewords_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), codebook_); }

    template<std::ranges::viewable_range R>
      requires byte_input_range<std::views::all_t<R>>
    [[nodiscard]] friend auto operator|(R &&range, codewords_closure const &adaptor)
      -> codewords_view<std::views::all_t<R>>
    { return adaptor(std::forward<R>(range)); }

  private:
    canonical_codebook codebook_;
  };

  /**
   * Creates a lazy byte-to-codeword adaptor, storing a copy of the codebook.
   *
   * @param codebook Validated mapping; every encountered symbol must be present.
   * Absent symbols throw std::invalid_argument during iteration.
   *
   * @code
   * auto words = bytes | dacoexec::detail::huffman::views::codewords(book);
   * @endcode
   *
   * @see codewords_view
   */
  [[nodiscard]] inline auto codewords(canonical_codebook codebook)
    -> ::dacoexec::detail::huffman::views::codewords_closure
  { return ::dacoexec::detail::huffman::views::codewords_closure{ codebook }; }

}// namespace views
}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_CODEWORDS_VIEW_HPP
