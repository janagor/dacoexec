#pragma once

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/huffman/canonical.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec {
namespace huffman {

// Single-pass lazy byte-to-codeword mapping with one staged value. Construction
// reads nothing; begin stages the first symbol. Copies of iterators alias the
// same position. The source and codebook live in one owned heap state, which
// survives moves of the view. Destroying its owner invalidates iterators.
// Source failures and absent symbols propagate and permanently end iteration.
template<std::ranges::view V>
  requires byte_input_range<V>
class codewords_view : public std::ranges::view_interface<codewords_view<V>>
{
  struct state
  {
    explicit state(V source, canonical_codebook codebook) : base_(std::move(source)), codebook_(std::move(codebook)) {}

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
        auto &position = *cursor_;
        auto const last = std::ranges::end(base_);
        if (position == last) {
          finished_ = true;
          return;
        }
        // Materialize range_value_t first so a proxy reference is converted
        // as the declared byte type, not as the proxy type itself.
        using source_value = std::ranges::range_value_t<V>;
        std::byte const value = dacoexec::detail::to_byte(static_cast<source_value>(*position));
        current_ = codebook_[std::to_integer<std::uint8_t>(value)];
        if (current_.length_ == 0) { throw std::invalid_argument("Symbol absent from Huffman codebook"); }
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto advance() -> void
    {
      if (finished_) { return; }
      try {
        ++(*cursor_);
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

  explicit codewords_view(V base, canonical_codebook codebook)
    : state_(std::make_unique<state>(std::move(base), std::move(codebook)))
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

    auto operator++(int) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &it, std::default_sentinel_t) noexcept -> bool
    { return it.state_ == nullptr || it.state_->finished_; }

  private:
    friend class codewords_view;

    explicit iterator(state *state) : state_(state) {}

    state *state_ = nullptr;
  };

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

    class codewords_closure
    {
    public:
      explicit codewords_closure(canonical_codebook codebook) : codebook_(std::move(codebook)) {}

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

  }// namespace detail

  [[nodiscard]] inline auto codewords(canonical_codebook codebook) -> detail::codewords_closure
  { return detail::codewords_closure{ std::move(codebook) }; }

}// namespace views
}// namespace huffman
}// namespace dacoexec
