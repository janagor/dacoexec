#pragma once

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
class buffered_bytes_view// NOLINT(readability-identifier-naming)
  : public std::ranges::view_interface<buffered_bytes_view<V>>
{
  struct State
  {
    State(V source, std::size_t window_size) : base(std::move(source)), capacity(window_size) {}

    void EnsureInitialized()
    {
      if (initialized) { return; }
      initialized = true;
      try {
        cursor.emplace(std::ranges::begin(base));
      } catch (...) {
        finished = true;
        throw;
      }
      Fill();
    }

    void Fill()
    {
      window.clear();
      index = 0;
      if (finished || !cursor.has_value()) {
        finished = true;
        return;
      }

      try {
        auto &position = *cursor;
        auto const last = std::ranges::end(base);
        while (window.size() < capacity) {
          if (position == last) { break; }
          // Materialize range_value_t first so a proxy reference is converted
          // as the declared byte type, not as the proxy type itself.
          using SourceValue = std::ranges::range_value_t<V>;
          std::byte const value = detail::ToByte(static_cast<SourceValue>(*position));
          if (window.size() == window.capacity()) {
            std::size_t const next = detail::NextBlockCapacity(window.capacity(), capacity);
            if (next > window.capacity()) { window.reserve(next); }
          }
          window.push_back(value);
          ++position;
        }
      } catch (...) {
        window.clear();
        index = 0;
        finished = true;
        throw;
      }

      if (window.empty()) { finished = true; }
    }

    void Advance()
    {
      if (finished) { return; }
      ++index;
      if (index < window.size()) { return; }
      Fill();
    }

    // base is declared before cursor so the source iterator is destroyed first.
    V base;
    std::size_t capacity = 0;
    std::vector<std::byte> window;
    std::optional<std::ranges::iterator_t<V>> cursor;
    std::size_t index = 0;
    bool initialized = false;
    bool finished = false;
  };

public:
  buffered_bytes_view() = default;

  buffered_bytes_view(V base, std::size_t capacity) : state_(std::make_unique<State>(std::move(base), capacity))
  {
    if (capacity == 0) {
      throw std::invalid_argument("dacoexec::views::buffered_bytes requires a positive buffer size");
    }
  }

  buffered_bytes_view(buffered_bytes_view const &) = delete;
  buffered_bytes_view &operator=(buffered_bytes_view const &) = delete;

  buffered_bytes_view(buffered_bytes_view &&) noexcept = default;

  buffered_bytes_view &operator=(buffered_bytes_view &&other) noexcept
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~buffered_bytes_view() = default;

  class Iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;
    using reference = std::byte;
    using pointer = void;

    [[nodiscard]] std::byte operator*() const { return state_->window[state_->index]; }

    Iterator &operator++()
    {
      state_->Advance();
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(Iterator const &it, std::default_sentinel_t) noexcept
    { return it.state_ == nullptr || it.state_->finished; }

  private:
    friend class buffered_bytes_view;

    explicit Iterator(State *state) : state_(state) {}

    State *state_ = nullptr;
  };

  [[nodiscard]] auto begin() -> Iterator
  {
    if (state_ == nullptr) { return Iterator{ nullptr }; }
    state_->EnsureInitialized();
    return Iterator{ state_.get() };
  }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::unique_ptr<State> state_;
};

namespace views {
  namespace detail {

    class BufferedBytesClosure
    {
    public:
      explicit constexpr BufferedBytesClosure(std::size_t capacity) : capacity_(capacity)
      {
        if (capacity_ == 0) {
          throw std::invalid_argument("dacoexec::views::buffered_bytes requires a positive buffer size");
        }
      }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const
      { return buffered_bytes_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), capacity_); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, BufferedBytesClosure const &adaptor)
      { return adaptor(std::forward<R>(range)); }

    private:
      std::size_t capacity_;
    };

  }// namespace detail

  // NOLINTNEXTLINE(readability-identifier-naming)
  [[nodiscard]] inline constexpr auto buffered_bytes(std::size_t capacity) -> detail::BufferedBytesClosure
  { return detail::BufferedBytesClosure{ capacity }; }

}// namespace views
}// namespace dacoexec
