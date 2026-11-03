#pragma once

#include <dacoexec/concepts.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dacoexec {
namespace detail {

  inline constexpr std::size_t kBlockGrowthOrigin = 4096;

  template<class T> constexpr std::byte ToByte(T value) noexcept
  {
    using Plain = std::remove_cv_t<T>;
    if constexpr (std::same_as<Plain, std::byte>) {
      return value;
    } else if constexpr (std::same_as<Plain, unsigned char>) {
      return static_cast<std::byte>(value);
    } else {
      static_assert(std::same_as<Plain, char> || std::same_as<Plain, signed char> || std::same_as<Plain, char8_t>);
      return static_cast<std::byte>(static_cast<unsigned char>(value));
    }
  }

  // Next vector capacity while filling one block. The first reservation is at
  // most 4096 bytes, later steps double, and the result never exceeds the
  // configured block size. Doubling is skipped when it would overflow size_t.
  [[nodiscard]] constexpr std::size_t NextBlockCapacity(std::size_t capacity, std::size_t block_size) noexcept
  {
    if (capacity >= block_size) { return block_size; }
    if (capacity == 0) { return block_size < kBlockGrowthOrigin ? block_size : kBlockGrowthOrigin; }
    if (capacity > (std::numeric_limits<std::size_t>::max() / 2)) { return block_size; }
    std::size_t const doubled = capacity * 2;
    return doubled < block_size ? doubled : block_size;
  }

}// namespace detail

// One owned slice of a byte stream.
//
// sequence starts at zero for the first block and increases by one. bytes is
// the block's own storage. The type is move-only so a block is transferred
// instead of copied. Moved-from blocks keep a valid sequence and a valid,
// unspecified buffer.
struct byte_block// NOLINT(readability-identifier-naming)
{
  std::uint64_t sequence = 0;
  std::vector<std::byte> bytes;

  byte_block() = default;

  byte_block(std::uint64_t seq, std::vector<std::byte> data) noexcept : sequence(seq), bytes(std::move(data)) {}

  byte_block(byte_block &&) noexcept = default;
  byte_block &operator=(byte_block &&) noexcept = default;

  byte_block(byte_block const &) = delete;
  byte_block &operator=(byte_block const &) = delete;

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
class blocks_view// NOLINT(readability-identifier-naming)
  : public std::ranges::view_interface<blocks_view<V>>
{
  struct State
  {
    State(V source, std::size_t size) : base(std::move(source)), block_size(size) {}

    void EnsureInitialized()
    {
      if (initialized) { return; }
      if (block_size == 0) { throw std::invalid_argument("dacoexec::views::blocks requires a positive block size"); }
      initialized = true;
      input = std::ranges::begin(base);
      LoadNextBlock();
    }

    void LoadNextBlock()
    {
      current.reset();
      if (finished || !input.has_value()) {
        finished = true;
        return;
      }

      auto &cursor = *input;
      auto const last = std::ranges::end(base);
      // End of input is a normal empty tail, including after sequence
      // 2^64-1. Overflow applies only when another byte is available.
      if (cursor == last) {
        finished = true;
        return;
      }
      if (sequence_limit_reached) {
        throw std::overflow_error("dacoexec::views::blocks sequence number space is exhausted");
      }

      std::vector<std::byte> bytes;
      try {
        while (bytes.size() < block_size) {
          if (cursor == last) { break; }
          // Materialize range_value_t first so a proxy reference is converted
          // as the declared byte type, not as the proxy type itself.
          using SourceValue = std::ranges::range_value_t<V>;
          std::byte const value = detail::ToByte(static_cast<SourceValue>(*cursor));
          if (bytes.size() == bytes.capacity()) {
            std::size_t const next = detail::NextBlockCapacity(bytes.capacity(), block_size);
            if (next > bytes.capacity()) { bytes.reserve(next); }
          }
          bytes.push_back(value);
          ++cursor;
        }
      } catch (...) {
        // The local buffer is discarded. Do not publish a short success.
        finished = true;
        throw;
      }

      if (bytes.empty()) {
        finished = true;
        return;
      }

      std::uint64_t const sequence = next_sequence;
      if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
        sequence_limit_reached = true;
      } else {
        ++next_sequence;
      }
      current.emplace(sequence, std::move(bytes));
    }

    // base is declared before input so the source cursor is destroyed first.
    V base;
    std::size_t block_size = 0;
    std::optional<std::ranges::iterator_t<V>> input;
    std::optional<byte_block> current;
    std::uint64_t next_sequence = 0;
    bool initialized = false;
    bool finished = false;
    bool sequence_limit_reached = false;
  };

public:
  blocks_view() = default;

  explicit blocks_view(V base, std::size_t block_size)
  {
    if (block_size == 0) { throw std::invalid_argument("dacoexec::views::blocks requires a positive block size"); }
    state_ = std::make_unique<State>(std::move(base), block_size);
  }

  blocks_view(blocks_view const &) = delete;
  blocks_view &operator=(blocks_view const &) = delete;

  blocks_view(blocks_view &&) noexcept = default;

  blocks_view &operator=(blocks_view &&other) noexcept
  {
    if (this != &other) { state_ = std::move(other.state_); }
    return *this;
  }

  ~blocks_view() = default;

  class Iterator
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
    [[nodiscard]] byte_block &&operator*() const { return std::move(state_->current.value()); }

    Iterator &operator++()
    {
      state_->LoadNextBlock();
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(Iterator const &it, std::default_sentinel_t) noexcept
    { return it.state_ == nullptr || !it.state_->current.has_value(); }

    [[nodiscard]] friend byte_block &&iter_move(Iterator const &it) { return std::move(it.state_->current.value()); }

  private:
    friend class blocks_view;

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

    class BlocksClosure
    {
    public:
      explicit constexpr BlocksClosure(std::size_t block_size) : block_size_(block_size)
      {
        if (block_size_ == 0) { throw std::invalid_argument("dacoexec::views::blocks requires a positive block size"); }
      }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] auto operator()(R &&range) const
      { return blocks_view<std::views::all_t<R>>(std::views::all(std::forward<R>(range)), block_size_); }

      template<std::ranges::viewable_range R>
        requires byte_input_range<std::views::all_t<R>>
      [[nodiscard]] friend auto operator|(R &&range, BlocksClosure const &adaptor)
      { return adaptor(std::forward<R>(range)); }

    private:
      std::size_t block_size_;
    };

  }// namespace detail

  // NOLINTNEXTLINE(readability-identifier-naming)
  [[nodiscard]] inline constexpr auto blocks(std::size_t block_size) -> detail::BlocksClosure
  { return detail::BlocksClosure{ block_size }; }

}// namespace views
}// namespace dacoexec
