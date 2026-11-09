//! \file
//! Lazy single-pass integer decode adaptation.

#ifndef DACOEXEC_VIEWS_INTEGER_DECODE_HPP
#define DACOEXEC_VIEWS_INTEGER_DECODE_HPP

#include <dacoexec/detail/views/integer_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::views {

/**
 * Construction reads nothing; begin() stages the first output. Iterator copies
 * share a single pass, and repeated begin() resumes it. Moving the view keeps
 * existing iterators valid; default and moved-from views are empty. Source
 * storage borrowed by the view must outlive iteration. Any exception permanently
 * ends the pass. Storage is bounded independently of input and codeword size.
 * Decodes exactly count values; trailing bits are left unread. Zero count
 * never starts the source. Truncation throws out_of_range; malformed values
 * and unary limits use the low-level decoder exception types.
 * Source increments are deferred until another bit is needed, so the final
 * requested value never stages subsequent input.
 */
template<std::ranges::view V, class Codec>
  requires std::ranges::input_range<V> && std::same_as<std::ranges::range_value_t<V>, bool>
class integer_decode_view : public std::ranges::view_interface<integer_decode_view<V, Codec>>
{
  struct state
  {
    explicit state(V base, Codec codec, std::uint64_t count) : base_(std::move(base)), codec_(codec), remaining_(count)
    {}

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        if (remaining_ == 0U) {
          finished_ = true;
          return;
        }
        cursor_.emplace(std::ranges::begin(base_));
        load();
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto read_bit() -> bool
    {
      if (!cursor_.has_value()) {
        throw std::out_of_range("dacoexec::detail::integer_decode_view source cursor is not initialized");
      }
      auto &position = cursor_.value();
      if (pending_advance_) {
        std::ranges::advance(position, 1);
        pending_advance_ = false;
      }
      if (position == std::ranges::end(base_)) {
        throw std::out_of_range("dacoexec::detail::integer_decode_view exhausted input");
      }
      auto const bit = static_cast<bool>(*position);
      pending_advance_ = true;
      return bit;
    }

    auto read_bits(unsigned count) -> std::uint64_t
    {
      if (count > integer_coding::integer_bits) {
        throw std::invalid_argument("dacoexec::detail::integer_decode_view width exceeds uint64_t");
      }
      std::uint64_t value = 0;
      for (unsigned index = 0; index < count; ++index) { value = (value << 1U) | (read_bit() ? 1U : 0U); }
      return value;
    }

    auto load() -> void
    {
      if (remaining_ == 0U) {
        finished_ = true;
        return;
      }
      current_ = codec_.decode(*this);
      --remaining_;
    }

    auto advance() -> void
    {
      if (finished_) { return; }
      try {
        load();
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    // Destroy the cursor before its source.
    V base_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    Codec codec_;
    std::uint64_t current_ = 0;
    std::uint64_t remaining_;
    bool pending_advance_ = false;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  integer_decode_view() = default;

  explicit integer_decode_view(V base, Codec codec, std::uint64_t count)
    : state_(std::make_unique<state>(std::move(base), codec, count))
  {}

  integer_decode_view(integer_decode_view const &) = delete;
  auto operator=(integer_decode_view const &) -> integer_decode_view & = delete;
  integer_decode_view(integer_decode_view &&) noexcept = default;

  auto operator=(integer_decode_view &&other) noexcept -> integer_decode_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~integer_decode_view() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::uint64_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    [[nodiscard]] auto operator*() const -> value_type { return state_->current_; }

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
    friend class integer_decode_view;

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

template<class Codec> class integer_decode_closure
{
public:
  constexpr explicit integer_decode_closure(Codec codec, std::uint64_t count) : codec_(codec), count_(count) {}

  template<std::ranges::viewable_range R>
    requires std::ranges::input_range<std::views::all_t<R>> && std::same_as<std::ranges::range_value_t<R>, bool>
  [[nodiscard]] auto operator()(R &&range) const -> integer_decode_view<std::views::all_t<R>, Codec>
  { return integer_decode_view<std::views::all_t<R>, Codec>(std::views::all(std::forward<R>(range)), codec_, count_); }

  template<std::ranges::viewable_range R>
    requires requires(integer_decode_closure const &adaptor, R &&range) { adaptor(std::forward<R>(range)); }
  [[nodiscard]] friend auto operator|(R &&range, integer_decode_closure const &adaptor)
    -> integer_decode_view<std::views::all_t<R>, Codec>
  { return adaptor(std::forward<R>(range)); }

private:
  Codec codec_;
  std::uint64_t count_;
};

}// namespace dacoexec::detail::views

namespace dacoexec::detail::views::decode {

/** Creates a lazy Gamma decoder of exactly count integers. */
[[nodiscard]] constexpr auto gamma(std::uint64_t count)
  -> ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::gamma_codec>
{
  return ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::gamma_codec>{
    ::dacoexec::detail::views::gamma_codec{}, count
  };
}

/** Creates a lazy Delta decoder of exactly count integers. */
[[nodiscard]] constexpr auto delta(std::uint64_t count)
  -> ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::delta_codec>
{
  return ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::delta_codec>{
    ::dacoexec::detail::views::delta_codec{}, count
  };
}

/** Creates a lazy Golomb decoder of exactly count integers. */
[[nodiscard]] constexpr auto
  golomb(std::uint32_t divisor, std::uint64_t count, integer_coding::decode_limits limits = {})
    -> ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::golomb_codec>
{
  integer_coding::validate_golomb(divisor);
  return ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::golomb_codec>{
    ::dacoexec::detail::views::golomb_codec{ .divisor_ = divisor, .limits_ = limits }, count
  };
}

/** Creates a lazy Rice decoder of exactly count integers. */
[[nodiscard]] constexpr auto rice(unsigned shift, std::uint64_t count, integer_coding::decode_limits limits = {})
  -> ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::rice_codec>
{
  integer_coding::validate_rice(shift);
  return ::dacoexec::detail::views::integer_decode_closure<::dacoexec::detail::views::rice_codec>{
    ::dacoexec::detail::views::rice_codec{ .shift_ = shift, .limits_ = limits }, count
  };
}

}// namespace dacoexec::detail::views::decode

#endif// DACOEXEC_VIEWS_INTEGER_DECODE_HPP
