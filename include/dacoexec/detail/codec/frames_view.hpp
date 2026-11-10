//! Shared lazy owning frame view and adaptor over format-specific parsers.
#ifndef DACOEXEC_CODEC_FRAMES_VIEW_HPP
#define DACOEXEC_CODEC_FRAMES_VIEW_HPP
#include <cstddef>
#include <dacoexec/detail/codec/frame_parser.hpp>
#include <exception>
#include <iterator>
#include <memory>
#include <ranges>
#include <utility>

namespace dacoexec::detail::codec {
//! Single-pass owning parser: iterator copies share state and dereference borrows
//! a stable const frame. iter_move explicitly transfers ownership. Construction reads nothing; begin() reads the first
//! frame. Moving the view preserves cursors. Source errors permanently end the pass. Payload states and CRC32 are
//! verified by decoding, not by the parser.
template<std::ranges::view V, class Format>
  requires byte_input_range<V>
class frames_view : public std::ranges::view_interface<frames_view<V, Format>>
{
  using state = frame_parser<V, Format>;

public:
  frames_view() = default;

  explicit frames_view(V base, std::size_t frame_limit)
  {
    if (frame_limit == 0 || frame_limit > Format::max_frame_size) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, Format::limit_message);
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

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = typename Format::frame_type;
    using difference_type = std::ptrdiff_t;
    using reference = typename Format::frame_type const &;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> typename Format::frame_type const &
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

    [[nodiscard]] friend auto iter_move(iterator const &cursor) noexcept -> typename Format::frame_type &&
    {
      if (cursor.state_ == nullptr || !cursor.state_->current_) { std::terminate(); }
      return std::move(*cursor.state_->current_);
    }

  private:
    friend class frames_view;

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

template<class Format> class frames_closure
{
public:
  explicit constexpr frames_closure(std::size_t frame_limit) : frame_limit_(frame_limit)
  {
    if (frame_limit_ == 0 || frame_limit_ > Format::max_frame_size) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, Format::limit_message);
    }
  }

  template<std::ranges::viewable_range R>
    requires byte_input_range<std::views::all_t<R>>
  [[nodiscard]] auto operator()(R &&range) const -> frames_view<std::views::all_t<R>, Format>
  { return frames_view<std::views::all_t<R>, Format>(std::views::all(std::forward<R>(range)), frame_limit_); }

  template<std::ranges::viewable_range R>
    requires byte_input_range<std::views::all_t<R>>
  [[nodiscard]] friend auto operator|(R &&range, frames_closure const &adaptor)
    -> frames_view<std::views::all_t<R>, Format>
  { return adaptor(std::forward<R>(range)); }

private:
  std::size_t frame_limit_;
};

}// namespace dacoexec::detail::codec
#endif
