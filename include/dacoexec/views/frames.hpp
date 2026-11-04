#ifndef DACOEXEC_VIEWS_FRAMES_HPP
#define DACOEXEC_VIEWS_FRAMES_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/detail/error.hpp>
#include <dacoexec/huffman/format.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec {

// Lazy single-pass owning frame parser. Moving the view preserves cursor state.
// Dereference transfers the staged value; increment invalidates its reference.
// DATA frames receive consecutive sequence numbers starting at zero. After
// sequence 2^64-1, another DATA frame throws overflow_error; END remains valid.
// Any source or format failure permanently ends the pass without publishing it.
template<std::ranges::view V>
  requires byte_input_range<V>
class frames_view : public std::ranges::view_interface<frames_view<V>>
{
  struct state
  {
    state(V source, std::size_t size) : base_(std::move(source)), frame_limit_(size) {}

    auto read_byte() -> std::byte
    {
      if (!input_.has_value()) { throw format_error("DHF1 input cursor is not initialized"); }
      auto &cursor = input_.value();
      if (cursor == std::ranges::end(base_)) { throw format_error("Truncated DHF1 archive or missing END"); }
      using source_value = std::ranges::range_value_t<V>;
      auto const value = detail::to_byte(static_cast<source_value>(*cursor));
      ++cursor;
      return value;
    }

    template<std::size_t n>
    auto read_array() -> std::array<std::byte, n>
    {
      std::array<std::byte, n> bytes{};
      for (auto &byte : bytes) { byte = read_byte(); }
      return bytes;
    }

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        input_.emplace(std::ranges::begin(base_));
        huffman::read_archive_header(read_array<huffman::archive_header_size>());
        load_next_frame();
      } catch (...) {
        finished_ = true;
        throw;
      }
    }

    auto load_next_frame() -> void
    {
      current_.reset();
      if (finished_) { return; }
      try {
        huffman::encoded_frame frame;
        frame.header_ = huffman::read_frame_header(read_array<huffman::frame_header_size>(), frame_limit_);
        auto const &header = frame.header_;
        if (header.kind_ == huffman::frame_kind::end) {
          if (!input_.has_value()) { throw format_error("DHF1 input cursor is not initialized"); }
          if (input_.value() != std::ranges::end(base_)) { throw format_error("Trailing bytes after END"); }
          finished_ = true;
          return;
        }
        if (sequence_limit_reached_) {
          throw std::overflow_error("dacoexec::views::frames sequence number space is exhausted");
        }
        auto &block = frame.block_;
        block.uncompressed_size_ = header.uncompressed_size_;
        if (header.kind_ == huffman::frame_kind::huffman) {
          block.encoding_ = huffman::block_encoding::canonical_huffman;
          for (auto &length : block.lengths_) { length = std::to_integer<std::uint8_t>(read_byte()); }
          try {
            if (std::ranges::all_of(block.lengths_, [](auto length) -> bool { return length == 0; })
                || !huffman::try_make_canonical_codebook(block.lengths_)) {
              throw format_error("Invalid canonical lengths");
            }
          } catch (std::invalid_argument const &error) {
            throw format_error(error.what());
          }
        }
        block.payload_.resize(header.payload_size_);
        for (auto &byte : block.payload_) { byte = read_byte(); }
        block.sequence_ = next_sequence_;
        if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
          sequence_limit_reached_ = true;
        } else {
          ++next_sequence_;
        }
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
    std::optional<huffman::encoded_frame> current_;
    std::uint64_t next_sequence_ = 0;
    bool sequence_limit_reached_ = false;
    bool initialized_ = false;
    bool finished_ = false;
  };

public:
  frames_view() = default;

  explicit frames_view(V base, std::size_t frame_limit)
  {
    if (frame_limit == 0 || frame_limit > huffman::max_frame_size) { throw std::invalid_argument("Invalid DHF1 frame limit"); }
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
    using value_type = huffman::encoded_frame;
    using difference_type = std::ptrdiff_t;
    using reference = huffman::encoded_frame &&;
    using pointer = void;

    // Returns the staged block as an rvalue so range-for can move it out.
    // A second dereference without ++ observes the moved-from block.
    // Dereferencing an end iterator is outside the input-iterator contract.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] auto operator*() const -> huffman::encoded_frame && { return std::move(state_->current_.value()); }

    auto operator++() -> iterator &
    {
      state_->load_next_frame();
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.state_ == nullptr || !cursor.state_->current_.has_value(); }

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] friend auto iter_move(iterator const &cursor) -> huffman::encoded_frame && { return std::move(cursor.state_->current_.value()); }

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

namespace views {
  namespace detail {

    class frames_closure
    {
    public:
      explicit constexpr frames_closure(std::size_t frame_limit) : frame_limit_(frame_limit)
      {
        if (frame_limit_ == 0 || frame_limit_ > huffman::max_frame_size) { throw std::invalid_argument("Invalid DHF1 frame limit"); }
      }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const -> frames_view<std::views::all_t<R>>
      { return frames_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), frame_limit_); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, frames_closure const &adaptor)
        -> frames_view<std::views::all_t<R>>
      { return adaptor(std::forward<R>(range)); }

    private:
      std::size_t frame_limit_;
    };

  }// namespace detail

  [[nodiscard]] constexpr auto frames(std::size_t frame_limit = huffman::max_frame_size) -> detail::frames_closure
  { return detail::frames_closure{ frame_limit }; }

}// namespace views
}// namespace dacoexec

#endif // DACOEXEC_VIEWS_FRAMES_HPP
