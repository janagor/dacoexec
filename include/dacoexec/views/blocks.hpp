#ifndef DACOEXEC_VIEWS_BLOCKS_HPP
#define DACOEXEC_VIEWS_BLOCKS_HPP

#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/buffer.hpp>
#include <dacoexec/detail/byte.hpp>

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

// One owned slice of a byte stream.
//
// sequence_ starts at zero for the first block and increases by one. bytes_ is
// the block's own storage. The type is move-only so a block is transferred
// instead of copied. Moved-from blocks keep a valid sequence and a valid,
// unspecified buffer.
struct byte_block
{
  std::uint64_t sequence_ = 0;
  std::vector<std::byte> bytes_;

  byte_block() = default;

  byte_block(std::uint64_t seq, std::vector<std::byte> data) noexcept : sequence_(seq), bytes_(std::move(data)) {}

  byte_block(byte_block &&) noexcept = default;
  auto operator=(byte_block &&) noexcept -> byte_block & = default;

  byte_block(byte_block const &) = delete;
  auto operator=(byte_block const &) -> byte_block & = delete;

  ~byte_block() = default;
};

// Lazy partition of a byte input range into owned blocks of at most block_size
// bytes.
//
// The view reads nothing until begin(). Each increment fills the next block.
// Only one block is staged. Moving a block out of the iterator transfers that
// storage; destroying the source afterwards leaves transferred blocks unchanged.
//
// The source, cursor, and staged block live in one heap state object. Moving
// the view transfers that object, so a view that has already started keeps its
// place in the input. Iterators refer to the state and remain usable after the
// view is moved. Destroying the view that owns the state invalidates them. A
// moved-from view is empty.
//
// Dereference rules:
// 1. A block can be moved out of the iterator's current value.
// 2. Dereferencing again without advancing observes a moved-from value.
// 3. References from dereference are invalidated when the iterator advances.
// 4. Blocks already moved out stay valid and independently owned.
//
// The range is single-pass. Copying an iterator aliases the same staged
// block; it does not restart the source or duplicate the buffer. Repeated
// begin() calls resume the in-progress pass. A block size of zero throws
// std::invalid_argument. Each block buffer starts at a capacity of at most
// 4096 bytes and then doubles until it reaches the configured block size.
// After the block numbered 2^64-1, a further non-empty block throws
// std::overflow_error. An exhausted source ends without that error. An
// exception from the source abandons the unfinished block and propagates
// unchanged.
template<std::ranges::view V>
  requires byte_input_range<V>
class blocks_view : public std::ranges::view_interface<blocks_view<V>>
{
  struct state
  {
    state(V source, std::size_t size) : base_(std::move(source)), block_size_(size) {}

    auto ensure_initialized() -> void
    {
      if (initialized_) { return; }
      if (block_size_ == 0) { throw std::invalid_argument("dacoexec::views::blocks requires a positive block size"); }
      initialized_ = true;
      input_ = std::ranges::begin(base_);
      load_next_block();
    }

    auto load_next_block() -> void
    {
      current_.reset();
      if (finished_ || !input_.has_value()) {
        finished_ = true;
        return;
      }

      auto &cursor = *input_;
      auto const last = std::ranges::end(base_);
      // End of input is a normal empty tail, including after sequence
      // 2^64-1. Overflow applies only when another byte is available.
      if (cursor == last) {
        finished_ = true;
        return;
      }
      if (sequence_limit_reached_) {
        throw std::overflow_error("dacoexec::views::blocks sequence number space is exhausted");
      }

      std::vector<std::byte> bytes;
      try {
        while (bytes.size() < block_size_) {
          if (cursor == last) { break; }
          // Materialize range_value_t first so a proxy reference is converted
          // as the declared byte type, not as the proxy type itself.
          using source_value = std::ranges::range_value_t<V>;
          std::byte const value = detail::to_byte(static_cast<source_value>(*cursor));
          if (bytes.size() == bytes.capacity()) {
            std::size_t const next = detail::next_block_capacity(bytes.capacity(), block_size_);
            if (next > bytes.capacity()) { bytes.reserve(next); }
          }
          bytes.push_back(value);
          std::ranges::advance(cursor, 1);
        }
      } catch (...) {
        // The local buffer is discarded. Do not publish a short success.
        finished_ = true;
        throw;
      }

      if (bytes.empty()) {
        finished_ = true;
        return;
      }

      std::uint64_t const sequence = next_sequence_;
      if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        sequence_limit_reached_ = true;
      } else {
        ++next_sequence_;
      }
      current_.emplace(sequence, std::move(bytes));
    }

    // base is declared before input so the source cursor is destroyed first.
    V base_;
    std::size_t block_size_ = 0;
    std::optional<std::ranges::iterator_t<V>> input_;
    std::optional<byte_block> current_;
    std::uint64_t next_sequence_ = 0;
    bool initialized_ = false;
    bool finished_ = false;
    bool sequence_limit_reached_ = false;
  };

public:
  blocks_view() = default;

  explicit blocks_view(V base, std::size_t block_size)
  {
    if (block_size == 0) { throw std::invalid_argument("dacoexec::views::blocks requires a positive block size"); }
    state_ = std::make_unique<state>(std::move(base), block_size);
  }

  blocks_view(blocks_view const &) = delete;
  auto operator=(blocks_view const &) -> blocks_view & = delete;

  blocks_view(blocks_view &&) noexcept = default;

  auto operator=(blocks_view &&other) noexcept -> blocks_view &
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~blocks_view() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = byte_block;
    using difference_type = std::ptrdiff_t;
    using reference = byte_block &&;
    using pointer = void;

    // Returns the staged block as an rvalue so range-for can move it out.
    // A second dereference without ++ observes the moved-from block.
    // Dereferencing an end iterator is outside the input-iterator contract.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] auto operator*() const -> byte_block && { return std::move(state_->current_.value()); }

    auto operator++() -> iterator &
    {
      state_->load_next_block();
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.state_ == nullptr || !cursor.state_->current_.has_value(); }

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] friend auto iter_move(iterator const &cursor) -> byte_block && { return std::move(cursor.state_->current_.value()); }

  private:
    friend class blocks_view;

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

    class blocks_closure
    {
    public:
      explicit constexpr blocks_closure(std::size_t block_size) : block_size_(block_size)
      {
        if (block_size_ == 0) { throw std::invalid_argument("dacoexec::views::blocks requires a positive block size"); }
      }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const -> blocks_view<std::views::all_t<R>>
      { return blocks_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), block_size_); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, blocks_closure const &adaptor)
        -> blocks_view<std::views::all_t<R>>
      { return adaptor(std::forward<R>(range)); }

    private:
      std::size_t block_size_;
    };

  }// namespace detail

  [[nodiscard]] constexpr auto blocks(std::size_t block_size) -> detail::blocks_closure
  { return detail::blocks_closure{ block_size }; }

}// namespace views
}// namespace dacoexec

#endif // DACOEXEC_VIEWS_BLOCKS_HPP
