//! \file
//! Lazy DAN1 framing parser with owned frame payloads.

#ifndef DACOEXEC_VIEWS_ANS_FRAMES_HPP
#define DACOEXEC_VIEWS_ANS_FRAMES_HPP

#include <dacoexec/ans/format.hpp>
#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/detail/error.hpp>

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

namespace dacoexec {

//! Single-pass owning parser: iterator copies share state and dereference borrows
//! a stable const frame. iter_move explicitly transfers ownership. Construction reads nothing; begin() reads the first
//! frame. Moving the view preserves cursors. Source errors permanently end the pass. Payload states and CRC32 are
//! verified by decoding, not by the parser.
template<std::ranges::view V>
  requires byte_input_range<V>
class ans_frames_view : public std::ranges::view_interface<ans_frames_view<V>>
{
  struct state
  {
    state(V source, std::size_t size) : base_(std::move(source)), frame_limit_(size) {}

    auto read_byte() -> std::byte
    {
      if (!input_.has_value()) { throw format_error("DAN1 input cursor is not initialized"); }
      auto &cursor = input_.value();
      if (cursor == std::ranges::end(base_)) { throw format_error("Truncated DAN1 archive or missing END"); }
      using source_value = std::ranges::range_value_t<V>;
      auto const value = detail::to_byte(static_cast<source_value>(*cursor));
      ++cursor;
      return value;
    }

    template<std::size_t n> auto read_array() -> std::array<std::byte, n>
    {
      std::array<std::byte, n> bytes{};
      std::ranges::generate(bytes, [this]() -> std::byte { return read_byte(); });
      return bytes;
    }

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        input_.emplace(std::ranges::begin(base_));
        ans::read_archive_header(read_array<ans::archive_header_size>());
        load_next_frame();
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto finish_archive() -> void
    {
      if (!input_.has_value()) { throw format_error("DAN1 input cursor is not initialized"); }
      if (input_.value() != std::ranges::end(base_)) { throw format_error("Trailing bytes after END"); }
      finished_ = true;
    }

    auto read_ans_model(ans::encoded_block &block) -> void
    {
      block.encoding_ = ans::block_encoding::rans;
      auto const precision = std::to_integer<unsigned>(read_byte());
      auto const count = dacoexec::detail::read_u16_le(read_array<sizeof(std::uint16_t)>());
      if (count == 0 || count > ans::alphabet_size) { throw format_error("Invalid ANS model entry count"); }
      ans::frequency_model<ans::alphabet_size>::table_type frequencies{};
      unsigned previous = 0;
      for (std::uint16_t entry = 0; entry < count; ++entry) {
        auto const symbol = std::to_integer<unsigned>(read_byte());
        if (entry != 0 && symbol <= previous) { throw format_error("Unordered ANS model symbols"); }
        frequencies.at(symbol) =
          std::uint32_t{ dacoexec::detail::read_u16_le(read_array<sizeof(std::uint16_t)>()) } + 1;
        previous = symbol;
      }
      try {
        block.model_.emplace(frequencies, precision);
      } catch (std::invalid_argument const &error) {
        throw format_error(error.what());
      }
    }

    auto read_data_block(ans::encoded_frame &frame) -> void
    {
      if (sequence_limit_reached_) {
        throw std::overflow_error("dacoexec::views::ans_frames sequence number space is exhausted");
      }
      auto &block = frame.block_;
      block.uncompressed_size_ = frame.header_.uncompressed_size_;
      if (frame.header_.kind_ == ans::frame_kind::rans) { read_ans_model(block); }
      block.payload_.resize(frame.header_.payload_size_);
      std::ranges::generate(block.payload_, [this]() -> std::byte { return read_byte(); });
      block.sequence_ = next_sequence_;
      if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        sequence_limit_reached_ = true;
      } else {
        ++next_sequence_;
      }
    }

    auto load_next_frame() -> void
    {
      current_.reset();
      if (finished_) { return; }
      try {
        ans::encoded_frame frame;
        frame.header_ = ans::read_frame_header(read_array<ans::frame_header_size>(), frame_limit_);
        if (frame.header_.kind_ == ans::frame_kind::end) {
          finish_archive();
          return;
        }
        read_data_block(frame);
        current_.emplace(std::move(frame));
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    // base is declared before input so the source cursor is destroyed first.
    V base_;
    std::size_t frame_limit_ = 0;
    std::optional<std::ranges::iterator_t<V>> input_;
    std::optional<ans::encoded_frame> current_;
    std::uint64_t next_sequence_ = 0;
    bool sequence_limit_reached_ = false;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  ans_frames_view() = default;

  explicit ans_frames_view(V base, std::size_t frame_limit)
  {
    if (frame_limit == 0 || frame_limit > ans::max_frame_size) {
      throw std::invalid_argument("Invalid DAN1 frame limit");
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
      if (!state_->current_) { throw std::bad_optional_access{}; }
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
  namespace detail {

    class ans_frames_closure
    {
    public:
      explicit constexpr ans_frames_closure(std::size_t frame_limit) : frame_limit_(frame_limit)
      {
        if (frame_limit_ == 0 || frame_limit_ > ans::max_frame_size) {
          throw std::invalid_argument("Invalid DAN1 frame limit");
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

  }// namespace detail

  [[nodiscard]] constexpr auto ans_frames(std::size_t frame_limit = ans::max_frame_size) -> detail::ans_frames_closure
  { return detail::ans_frames_closure{ frame_limit }; }

}// namespace views
}// namespace dacoexec

#endif// DACOEXEC_VIEWS_ANS_FRAMES_HPP
