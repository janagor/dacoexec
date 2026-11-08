//! \file
//! Lazy single-pass integer encode adaptation.

#ifndef DACOEXEC_VIEWS_INTEGER_ENCODE_HPP
#define DACOEXEC_VIEWS_INTEGER_ENCODE_HPP

#include <dacoexec/views/detail/integer_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::views::detail {

/**
 * Construction reads nothing; begin() stages the first output. Iterator copies
 * share a single pass, and repeated begin() resumes it. Moving the view keeps
 * existing iterators valid; default and moved-from views are empty. Source
 * storage borrowed by the view must outlive iteration. Any exception permanently
 * ends the pass. Storage is bounded independently of input and codeword size.
 * Yields bool bits without packing bytes.
 */
template<std::ranges::view V, class Codec>
  requires integer_coding::unsigned_integer_range<V>
class integer_encode_view : public std::ranges::view_interface<integer_encode_view<V, Codec>>
{
  struct state
  {
    explicit state(V base, Codec codec) : base_(std::move(base)), codec_(codec) {}

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        cursor_.emplace(std::ranges::begin(base_));
        load();
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto load() -> void
    {
      if (!cursor_.has_value()) {
        finished_ = true;
        return;
      }
      auto const &position = cursor_.value();
      if (position == std::ranges::end(base_)) {
        finished_ = true;
        return;
      }
      using source_value = std::ranges::range_value_t<V>;
      current_ = codec_.encode(static_cast<std::uint64_t>(static_cast<source_value>(*position)));
    }

    auto advance() -> void
    {
      if (finished_ || !cursor_.has_value()) { return; }
      try {
        current_.advance();
        if (current_.empty()) {
          std::ranges::advance(cursor_.value(), 1);
          load();
        }
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    // Destroy the cursor before its source.
    V base_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    Codec codec_;
    detail::integer_codeword current_;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  integer_encode_view() = default;

  explicit integer_encode_view(V base, Codec codec) : state_(std::make_unique<state>(std::move(base), codec)) {}

  integer_encode_view(integer_encode_view const &) = delete;
  auto operator=(integer_encode_view const &) -> integer_encode_view & = delete;
  integer_encode_view(integer_encode_view &&) noexcept = default;

  auto operator=(integer_encode_view &&other) noexcept -> integer_encode_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~integer_encode_view() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = bool;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> value_type { return state_->current_.bit(); }

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
    friend class integer_encode_view;

    explicit iterator(state *position) : state_(position) {}

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

template<class Codec> class integer_encode_closure
{
public:
  constexpr explicit integer_encode_closure(Codec codec) : codec_(codec) {}

  template<std::ranges::viewable_range R>
    requires integer_coding::unsigned_integer_range<std::views::all_t<R>>
  [[nodiscard]] auto operator()(R &&range) const -> integer_encode_view<std::views::all_t<R>, Codec>
  { return integer_encode_view<std::views::all_t<R>, Codec>(std::views::all(std::forward<R>(range)), codec_); }

  template<std::ranges::viewable_range R>
    requires requires(integer_encode_closure const &adaptor, R &&range) { adaptor(std::forward<R>(range)); }
  [[nodiscard]] friend auto operator|(R &&range, integer_encode_closure const &adaptor)
    -> integer_encode_view<std::views::all_t<R>, Codec>
  { return adaptor(std::forward<R>(range)); }

private:
  Codec codec_;
};

}// namespace dacoexec::views::detail

namespace dacoexec::views::encode {

/** Creates a lazy Gamma bit encoder. */
[[nodiscard]] constexpr auto gamma() -> detail::integer_encode_closure<detail::gamma_codec>
{ return detail::integer_encode_closure<detail::gamma_codec>{ detail::gamma_codec{} }; }

/** Creates a lazy Delta bit encoder. */
[[nodiscard]] constexpr auto delta() -> detail::integer_encode_closure<detail::delta_codec>
{ return detail::integer_encode_closure<detail::delta_codec>{ detail::delta_codec{} }; }

/** Creates a lazy Golomb bit encoder. */
[[nodiscard]] constexpr auto golomb(std::uint32_t divisor) -> detail::integer_encode_closure<detail::golomb_codec>
{
  integer_coding::detail::validate_golomb(divisor);
  return detail::integer_encode_closure<detail::golomb_codec>{ detail::golomb_codec{ .divisor_ = divisor } };
}

/** Creates a lazy Rice bit encoder. */
[[nodiscard]] constexpr auto rice(unsigned shift) -> detail::integer_encode_closure<detail::rice_codec>
{
  integer_coding::detail::validate_rice(shift);
  return detail::integer_encode_closure<detail::rice_codec>{ detail::rice_codec{ .shift_ = shift } };
}

}// namespace dacoexec::views::encode

#endif// DACOEXEC_VIEWS_INTEGER_ENCODE_HPP
