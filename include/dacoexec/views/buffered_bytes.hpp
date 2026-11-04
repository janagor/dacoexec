#ifndef DACOEXEC_VIEWS_BUFFERED_BYTES_HPP
#define DACOEXEC_VIEWS_BUFFERED_BYTES_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/buffer.hpp>
#include <dacoexec/detail/byte.hpp>

#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec {

// Lazy window over a byte input range.
//
// The view yields one std::byte at a time, but it reads the source in windows
// of at most `capacity` bytes. A window is filled only when the previous one
// has been consumed, so a byte sequence continues across those boundaries.
// Character values are converted when they enter the window. The conversion
// preserves high bits and does not depend on host endianness.
//
// Construction reads nothing. begin() fills the first window. The source, the
// window, and the read cursor live in one heap state object. Moving the view
// transfers that object, so a view that has already started keeps its place,
// including a partly consumed window. Iterators refer to the state and remain
// usable after the view is moved. Destroying the view that owns the state
// invalidates them. A moved-from view is empty.
//
// The range is single-pass. Copying an iterator aliases the same byte; it
// does not restart the source or duplicate the window. Repeated begin() calls
// resume the in-progress pass. A buffer size of zero throws
// std::invalid_argument. The window buffer starts at a capacity of at most
// 4096 bytes and doubles until it reaches the requested size. An empty source
// allocates nothing. An exception from the source propagates unchanged. The
// unfinished window is discarded, and later iteration does not resume.
template<std::ranges::view V>
  requires byte_input_range<V>
class buffered_bytes_view : public std::ranges::view_interface<buffered_bytes_view<V>>
{
  struct state
  {
    state(V source, std::size_t window_size) : base_(std::move(source)), capacity_(window_size) {}

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
      fill();
    }

    auto fill() -> void
    {
      window_.clear();
      index_ = 0;
      if (finished_ || !cursor_.has_value()) {
        finished_ = true;
        return;
      }

      try {
        auto &position = *cursor_;
        auto const last = std::ranges::end(base_);
        while (window_.size() < capacity_) {
          if (position == last) { break; }
          // Materialize range_value_t first so a proxy reference is converted
          // as the declared byte type, not as the proxy type itself.
          using source_value = std::ranges::range_value_t<V>;
          std::byte const value = detail::to_byte(static_cast<source_value>(*position));
          if (window_.size() == window_.capacity()) {
            std::size_t const next = detail::next_block_capacity(window_.capacity(), capacity_);
            if (next > window_.capacity()) { window_.reserve(next); }
          }
          window_.push_back(value);
          ++position;
        }
      } catch (...) {
        window_.clear();
        index_ = 0;
        finished_ = true;
        throw;
      }

      if (window_.empty()) { finished_ = true; }
    }

    auto advance() -> void
    {
      if (finished_) { return; }
      ++index_;
      if (index_ < window_.size()) { return; }
      fill();
    }

    // base is declared before cursor so the source iterator is destroyed first.
    V base_;
    std::size_t capacity_ = 0;
    std::vector<std::byte> window_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    std::size_t index_ = 0;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  buffered_bytes_view() = default;

  buffered_bytes_view(V base, std::size_t capacity) : state_(std::make_unique<state>(std::move(base), capacity))
  {
    if (capacity == 0) {
      throw std::invalid_argument("dacoexec::views::buffered_bytes requires a positive buffer size");
    }
  }

  buffered_bytes_view(buffered_bytes_view const &) = delete;
  auto operator=(buffered_bytes_view const &) -> buffered_bytes_view & = delete;

  buffered_bytes_view(buffered_bytes_view &&) noexcept = default;

  auto operator=(buffered_bytes_view &&other) noexcept -> buffered_bytes_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~buffered_bytes_view() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;
    using reference = std::byte;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> std::byte { return state_->window_.at(state_->index_); }

    auto operator++() -> iterator &
    {
      state_->advance();
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.state_ == nullptr || cursor.state_->finished_; }

  private:
    friend class buffered_bytes_view;

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

    class buffered_bytes_closure
    {
    public:
      explicit constexpr buffered_bytes_closure(std::size_t capacity) : capacity_(capacity)
      {
        if (capacity_ == 0) {
          throw std::invalid_argument("dacoexec::views::buffered_bytes requires a positive buffer size");
        }
      }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const -> buffered_bytes_view<std::views::all_t<R>>
      { return buffered_bytes_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), capacity_); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, buffered_bytes_closure const &adaptor)
        -> buffered_bytes_view<std::views::all_t<R>>
      { return adaptor(std::forward<R>(range)); }

    private:
      std::size_t capacity_;
    };

  }// namespace detail

  [[nodiscard]] constexpr auto buffered_bytes(std::size_t capacity) -> detail::buffered_bytes_closure
  { return detail::buffered_bytes_closure{ capacity }; }

}// namespace views
}// namespace dacoexec

#endif // DACOEXEC_VIEWS_BUFFERED_BYTES_HPP
