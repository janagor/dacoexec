#pragma once

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
  // `index` must be less than kBitsPerByte.
  [[nodiscard]] constexpr bool MsbFirstBit(std::uint8_t byte, unsigned index) noexcept
  { return ((static_cast<unsigned>(byte) >> ((kBitsPerByte - 1U) - index)) & 1U) != 0U; }

}// namespace detail

// Lazy view of the bits in a byte input range.
//
// Each byte becomes eight bool values, bit 7 first and bit 0 last. The order
// does not depend on the host endianness. An empty byte range yields no bits.
// The view never allocates a bit buffer. It caches one source byte, so the
// underlying iterator is dereferenced once per byte rather than once per bit.
//
// Construction reads nothing. begin() reads the first byte. Advancing past
// bit 0 reads the next byte, or reaches the end when the source is exhausted.
//
// The source and the one-byte cache live in one heap state object. Moving the
// view transfers that object, so a view that has already started keeps its
// place in the input. Iterators refer to the state and remain usable after
// the view is moved. Destroying the view that owns the state invalidates
// them. A moved-from view is empty.
//
// The range is single-pass. Copying an iterator aliases the same bit
// position; it does not restart the source. Repeated begin() calls resume the
// in-progress pass. An exception from the source propagates unchanged. No bit
// is invented for the failed read, and later iteration does not resume.
template<std::ranges::view V>
  requires byte_input_range<V>
class bits_view// NOLINT(readability-identifier-naming)
  : public std::ranges::view_interface<bits_view<V>>
{
  struct State
  {
    explicit State(V source) : base(std::move(source)) {}

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
      LoadNextByte();
    }

    void LoadNextByte()
    {
      if (finished || !cursor.has_value()) {
        finished = true;
        return;
      }

      try {
        auto &position = *cursor;
        auto const last = std::ranges::end(base);
        if (position == last) {
          finished = true;
          return;
        }
        // Materialize range_value_t first so a proxy reference is converted
        // as the declared byte type, not as the proxy type itself.
        using SourceValue = std::ranges::range_value_t<V>;
        std::byte const value = detail::ToByte(static_cast<SourceValue>(*position));
        current_byte = std::to_integer<std::uint8_t>(value);
        bit_index = 0;
      } catch (...) {
        finished = true;
        throw;
      }
    }

    void Advance()
    {
      if (finished) { return; }
      if (bit_index + 1U < detail::kBitsPerByte) {
        ++bit_index;
        return;
      }

      try {
        ++(*cursor);
      } catch (...) {
        finished = true;
        throw;
      }
      LoadNextByte();
    }

    // base is declared before cursor so the source iterator is destroyed first.
    V base;
    std::optional<std::ranges::iterator_t<V>> cursor;
    std::uint8_t current_byte = 0;
    unsigned bit_index = 0;
    bool initialized = false;
    bool finished = false;
  };

public:
  bits_view() = default;

  explicit bits_view(V base) : state_(std::make_unique<State>(std::move(base))) {}

  bits_view(bits_view const &) = delete;
  bits_view &operator=(bits_view const &) = delete;

  bits_view(bits_view &&) noexcept = default;

  bits_view &operator=(bits_view &&other) noexcept
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~bits_view() = default;

  class Iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = bool;
    using difference_type = std::ptrdiff_t;
    using reference = bool;
    using pointer = void;

    [[nodiscard]] bool operator*() const { return detail::MsbFirstBit(state_->current_byte, state_->bit_index); }

    Iterator &operator++()
    {
      state_->Advance();
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(Iterator const &it, std::default_sentinel_t) noexcept
    { return it.state_ == nullptr || it.state_->finished; }

  private:
    friend class bits_view;

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

    class BitsClosure
    {
    public:
      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const
      { return bits_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range))); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, BitsClosure const &adaptor)
      { return adaptor(std::forward<R>(range)); }
    };

  }// namespace detail

  // NOLINTNEXTLINE(readability-identifier-naming)
  [[nodiscard]] inline constexpr auto bits() noexcept -> detail::BitsClosure { return detail::BitsClosure{}; }

}// namespace views
}// namespace dacoexec
