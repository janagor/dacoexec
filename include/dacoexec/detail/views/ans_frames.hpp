//! \file
//! Lazy DAN1 framing parser with owned frame payloads.

#ifndef DACOEXEC_VIEWS_ANS_FRAMES_HPP
#define DACOEXEC_VIEWS_ANS_FRAMES_HPP

#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/detail/views/ans_frame_parser.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec::detail {

//! Single-pass owning parser: iterator copies share state and dereference borrows
//! a stable const frame. iter_move explicitly transfers ownership. Construction reads nothing; begin() reads the first
//! frame. Moving the view preserves cursors. Source errors permanently end the pass. Payload states and CRC32 are
//! verified by decoding, not by the parser.
template<std::ranges::view V>
  requires byte_input_range<V>
class ans_frames_view : public std::ranges::view_interface<ans_frames_view<V>>
{
  using state = support::ans_frame_parser<V>;

public:
  ans_frames_view() = default;

  explicit ans_frames_view(V base, std::size_t frame_limit)
  {
    if (frame_limit == 0 || frame_limit > ans::max_frame_size) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid DAN1 frame limit");
    }
    state_ = std::make_unique<state>(std::move(base), frame_limit);
  }

  ans_frames_view(ans_frames_view const &) = delete;
  auto operator=(ans_frames_view const &) -> ans_frames_view & = delete;

  ans_frames_view(ans_frames_view &&) noexcept = default;

  auto operator=(ans_frames_view &&other) noexcept -> ans_frames_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~ans_frames_view() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = ans::encoded_frame;
    using difference_type = std::ptrdiff_t;
    using reference = ans::encoded_frame const &;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> ans::encoded_frame const &
    {
      if (!state_->current_) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Iterator has no current value");
      }
      return *state_->current_;
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

    [[nodiscard]] friend auto iter_move(iterator const &cursor) noexcept -> ans::encoded_frame &&
    {
      if (cursor.state_ == nullptr || !cursor.state_->current_) { std::terminate(); }
      return std::move(*cursor.state_->current_);
    }

  private:
    friend class ans_frames_view;

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


  class ans_frames_closure
  {
  public:
    explicit constexpr ans_frames_closure(std::size_t frame_limit) : frame_limit_(frame_limit)
    {
      if (frame_limit_ == 0 || frame_limit_ > ans::max_frame_size) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid DAN1 frame limit");
      }
    }

    template<std::ranges::viewable_range R>
      requires byte_input_range<std::views::all_t<R>>
    [[nodiscard]] auto operator()(R &&range) const -> ans_frames_view<std::views::all_t<R>>
    { return ans_frames_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), frame_limit_); }

    template<std::ranges::viewable_range R>
      requires byte_input_range<std::views::all_t<R>>
    [[nodiscard]] friend auto operator|(R &&range, ans_frames_closure const &adaptor)
      -> ans_frames_view<std::views::all_t<R>>
    { return adaptor(std::forward<R>(range)); }

  private:
    std::size_t frame_limit_;
  };

  [[nodiscard]] constexpr auto ans_frames(std::size_t frame_limit = ans::max_frame_size)
    -> ::dacoexec::detail::views::ans_frames_closure
  { return ::dacoexec::detail::views::ans_frames_closure{ frame_limit }; }

}// namespace views
}// namespace dacoexec::detail

#endif// DACOEXEC_VIEWS_ANS_FRAMES_HPP
