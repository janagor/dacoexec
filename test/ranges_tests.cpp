#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/buffer.hpp>
#include <dacoexec/huffman/canonical.hpp>
#include <dacoexec/huffman/codewords_view.hpp>
#include <dacoexec/huffman/tree.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/views/blocks.hpp>
#include "huffman_test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dacoexec/dacoexec.hpp> // IWYU pragma: keep -- Exercise the public umbrella header.

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr auto initial_capacity = 4096;
constexpr auto capacity_limit = 10000;
constexpr auto doubled_capacity = initial_capacity * 2U;
constexpr auto short_input_size = 5;
constexpr auto two_block_input_size = 8;
constexpr auto three_block_input_size = 9;
constexpr auto single_byte_block_count = 7;
constexpr auto multi_block_input_size = 10;
constexpr auto second_block_failure_read = 6;


struct source_stats
{
  std::size_t begin_calls_ = 0;
  std::size_t dereferences_ = 0;
  std::size_t increments_ = 0;
  std::optional<std::size_t> fail_at_dereference_;
  bool fail_at_begin_ = false;
  std::optional<std::size_t> fail_at_increment_;
};

// Move-only, single-pass byte source. begin() does not rewind: every iterator
// shares one cursor stored in the source, and the cursor only advances.
class byte_source : public std::ranges::view_interface<byte_source>
{
public:
  byte_source(std::vector<std::byte> data, std::shared_ptr<source_stats> stats)
    : data_(std::move(data)), stats_(std::move(stats))
  {}

  byte_source(byte_source const &) = delete;
  auto operator=(byte_source const &) -> byte_source & = delete;
  byte_source(byte_source &&) = default;
  auto operator=(byte_source &&) -> byte_source & = default;
  ~byte_source() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    iterator(iterator const &) = delete;
    auto operator=(iterator const &) -> iterator & = delete;
    iterator(iterator &&) = default;
    auto operator=(iterator &&) -> iterator & = default;
    ~iterator() = default;

    [[nodiscard]] auto operator*() const -> std::byte
    {
      ++parent_->stats_->dereferences_;
      if (parent_->stats_->fail_at_dereference_.has_value()
          && parent_->stats_->dereferences_ >= *parent_->stats_->fail_at_dereference_) {
        throw std::ios_base::failure("injected read failure");
      }
      return parent_->data_.at(parent_->cursor_);
    }

    auto operator++() -> iterator &
    {
      ++parent_->stats_->increments_;
      if (parent_->stats_->fail_at_increment_.has_value()
          && parent_->stats_->increments_ >= *parent_->stats_->fail_at_increment_) {
        throw std::ios_base::failure("injected increment failure");
      }
      ++parent_->cursor_;
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    // A member, not a hidden friend: GCC 12 does not let a friend defined in
    // this nested iterator reach byte_source's private state.
    [[nodiscard]] auto operator==([[maybe_unused]] std::default_sentinel_t sentinel) const noexcept -> bool
    { return parent_->cursor_ == parent_->data_.size(); }

  private:
    friend class byte_source;

    explicit iterator(byte_source *parent) : parent_(parent) {}

    byte_source *parent_ = nullptr;
  };

  [[nodiscard]] auto begin() -> iterator
  {
    ++stats_->begin_calls_;
    if (stats_->fail_at_begin_) { throw std::ios_base::failure("injected begin failure"); }
    return iterator{ this };
  }

  [[nodiscard]] static auto end() noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
  std::shared_ptr<source_stats> stats_;
  // Retained so a second begin() cannot rewind a consumed source. The iterator
  // itself is move-only and carries the cursor for the single pass.
  std::size_t cursor_ = 0;
};

static_assert(std::ranges::view<byte_source>);
static_assert(std::ranges::input_range<byte_source>);
static_assert(!std::ranges::forward_range<byte_source>);
static_assert(!std::ranges::sized_range<byte_source>);
static_assert(!std::copyable<byte_source>);
static_assert(dacoexec::byte_input_range<byte_source>);
static_assert(dacoexec::byte_input_range<std::vector<std::byte>>);
static_assert(dacoexec::byte_input_range<std::array<std::byte, 1>>);
static_assert(dacoexec::byte_input_range<std::span<std::byte const>>);
static_assert(dacoexec::byte_input_range<std::vector<unsigned char>>);
static_assert(dacoexec::byte_input_range<std::vector<char>>);
static_assert(dacoexec::byte_input_range<std::vector<signed char>>);
static_assert(dacoexec::byte_input_range<std::vector<char8_t>>);
static_assert(!dacoexec::byte_input_range<std::vector<int>>);

template<class R>
concept can_pipe_blocks = requires(R &&range) { std::forward<R>(range) | dacoexec::views::blocks(std::size_t{ 4 }); };

static_assert(can_pipe_blocks<std::vector<std::byte> &>);
static_assert(can_pipe_blocks<std::vector<std::byte>>);
static_assert(can_pipe_blocks<byte_source>);
static_assert(!can_pipe_blocks<byte_source &>);
static_assert(!can_pipe_blocks<std::vector<int> &>);

using block_range = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::blocks(std::size_t{ 4 }));

static_assert(std::ranges::view<block_range>);
static_assert(std::ranges::input_range<block_range>);
static_assert(!std::ranges::forward_range<block_range>);
static_assert(!std::ranges::borrowed_range<block_range>);
static_assert(std::same_as<std::ranges::range_value_t<block_range>, dacoexec::byte_block>);
static_assert(std::same_as<std::iter_reference_t<std::ranges::iterator_t<block_range>>, dacoexec::byte_block &&>);
static_assert(std::input_iterator<std::ranges::iterator_t<block_range>>);
static_assert(std::movable<dacoexec::byte_block>);
static_assert(!std::copyable<dacoexec::byte_block>);
static_assert(std::is_nothrow_move_constructible_v<dacoexec::byte_block>);
static_assert(std::is_nothrow_move_assignable_v<dacoexec::byte_block>);
static_assert(std::movable<block_range>);
static_assert(!std::copyable<block_range>);
static_assert(std::same_as<decltype(dacoexec::byte_block::sequence_), std::uint64_t>);
static_assert(dacoexec::detail::next_block_capacity(0, std::numeric_limits<std::size_t>::max()) == initial_capacity);
static_assert(dacoexec::detail::next_block_capacity(initial_capacity, capacity_limit) == doubled_capacity);
static_assert(dacoexec::detail::next_block_capacity(doubled_capacity, capacity_limit) == capacity_limit);
static_assert(dacoexec::detail::next_block_capacity((std::numeric_limits<std::size_t>::max() / 2) + 1,
                std::numeric_limits<std::size_t>::max())
              == std::numeric_limits<std::size_t>::max());

[[nodiscard]] auto make_bytes(std::size_t count) -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(count);
  for (std::size_t index = 0; index < count; ++index) {
    bytes.at(index) = static_cast<std::byte>(static_cast<unsigned char>(index));
  }
  return bytes;
}

[[nodiscard]] auto flatten(std::vector<dacoexec::byte_block> const &blocks) -> std::vector<std::byte>
{
  std::vector<std::byte> out;
  for (dacoexec::byte_block const &block : blocks) { out.insert(out.end(), block.bytes_.begin(), block.bytes_.end()); }
  return out;
}

// Distinct parameter roles in test helpers.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto check_partition(std::size_t count, std::size_t block_size, std::span<std::size_t const> expected_sizes) -> void
{
  std::vector<std::byte> const expected = make_bytes(count);
  std::vector<std::byte> input = expected;
  std::vector<std::size_t> sizes;
  std::vector<std::byte> flattened;
  std::uint64_t sequence = 0;
  for (auto block : input | dacoexec::views::blocks(block_size)) {
    REQUIRE(block.sequence_ == sequence);
    ++sequence;
    sizes.push_back(block.bytes_.size());
    flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
  }
  REQUIRE(sizes.size() == expected_sizes.size());
  REQUIRE(std::ranges::equal(sizes, expected_sizes));
  REQUIRE(flattened == expected);
}

}// namespace

// * returns a proxy. value_type is still std::byte, so the range models
// byte_input_range. common_reference is specialized so the proxy satisfies
// indirectly_readable.
struct byte_proxy
{
  std::byte value_{};
  // Implicit conversion is required by indirectly_readable.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  constexpr operator std::byte() const { return value_; }
};

template<template<class> class QualByte, template<class> class QualProxy>
struct std::basic_common_reference<std::byte, byte_proxy, QualByte, QualProxy>
{
  using type = std::byte;
};

template<template<class> class QualProxy, template<class> class QualByte>
struct std::basic_common_reference<byte_proxy, std::byte, QualProxy, QualByte>
{
  using type = std::byte;
};

class proxy_bytes : public std::ranges::view_interface<proxy_bytes>
{
public:
  explicit proxy_bytes(std::vector<std::byte> data) : data_(std::move(data)) {}

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    [[nodiscard]] auto operator*() const -> byte_proxy { return byte_proxy{ *ptr_ }; }

    auto operator++() -> iterator &
    {
      // Advance the source iterator within its sentinel.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      ++ptr_;
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.ptr_ == cursor.last_; }

  private:
    friend class proxy_bytes;

    iterator(std::byte const *ptr, std::byte const *last) : ptr_(ptr), last_(last) {}

    std::byte const *ptr_ = nullptr;
    std::byte const *last_ = nullptr;
  };

  // Advance the source iterator within its sentinel.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  [[nodiscard]] auto begin() -> iterator { return iterator{ data_.data(), data_.data() + data_.size() }; }

  [[nodiscard]] static auto end() noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
};

static_assert(std::input_iterator<proxy_bytes::iterator>);
static_assert(std::ranges::input_range<proxy_bytes>);
static_assert(dacoexec::byte_input_range<proxy_bytes>);
static_assert(std::same_as<std::ranges::range_value_t<proxy_bytes>, std::byte>);
static_assert(std::same_as<std::iter_reference_t<proxy_bytes::iterator>, byte_proxy>);

TEST_CASE("blocks reads a proxy-backed byte range", "[blocks]")
{
  std::vector<std::byte> const expected = make_bytes(10);
  std::vector<std::size_t> sizes;
  std::vector<std::byte> flattened;
  std::uint64_t sequence = 0;
  for (auto block : proxy_bytes{ expected } | dacoexec::views::blocks(4)) {
    REQUIRE(block.sequence_ == sequence);
    ++sequence;
    sizes.push_back(block.bytes_.size());
    flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
  }
  REQUIRE(sizes == std::vector<std::size_t>{ 4, 4, 2 });
  REQUIRE(flattened == expected);
}

TEST_CASE("blocks partitions bytes and numbers blocks from zero", "[blocks]")
{
  std::vector<std::size_t> const one(7, 1);
  std::array<std::size_t, 2> const five{ { 4, 1 } };
  std::array<std::size_t, 2> const eight{ { 4, 4 } };
  std::array<std::size_t, 3> const nine{ { 4, 4, 1 } };
  std::array<std::size_t, 4> const ten{ { 3, 3, 3, 1 } };
  std::array<std::size_t, 1> const only_one{ { 1 } };
  std::array<std::size_t, 1> const exact{ { 4 } };

  check_partition(0, 4, {});
  check_partition(1, 4, only_one);
  check_partition(4, 4, exact);
  check_partition(short_input_size, 4, five);
  check_partition(two_block_input_size, 4, eight);
  check_partition(three_block_input_size, 4, nine);
  check_partition(single_byte_block_count, 1, one);
  check_partition(multi_block_input_size, 3, ten);
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("blocks accepts standard ranges, call syntax, and owning rvalues", "[blocks]")
{
  std::vector<std::byte> const expected = make_bytes(5);

  {
    std::vector<std::byte> input = expected;
    std::vector<dacoexec::byte_block> blocks;
    for (auto block : dacoexec::views::blocks(4)(input)) { blocks.push_back(std::move(block)); }
    REQUIRE(blocks.size() == 2);
    REQUIRE(blocks.at(0).bytes_.size() == 4);
    REQUIRE(blocks.at(1).bytes_.size() == 1);
    REQUIRE(flatten(blocks) == expected);
  }

  {
    std::array<std::byte, short_input_size> input{};
    std::ranges::copy(expected, input.begin());
    std::vector<std::byte> flattened;
    std::uint64_t sequence = 0;
    for (auto block : input | dacoexec::views::blocks(4)) {
      REQUIRE(block.sequence_ == sequence);
      ++sequence;
      flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
    }
    REQUIRE(flattened == expected);
  }

  {
    std::vector<std::byte> storage = expected;
    std::span<std::byte const> const input{ storage };
    std::vector<std::byte> flattened;
    for (auto block : input | dacoexec::views::blocks(2)) {
      flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
    }
    REQUIRE(flattened == expected);
  }

  {
    std::vector<dacoexec::byte_block> blocks;
    for (auto block : make_bytes(5) | dacoexec::views::blocks(4)) { blocks.push_back(std::move(block)); }
    REQUIRE(flatten(blocks) == expected);
  }
}

TEST_CASE("blocks converts character bytes and preserves high bits", "[blocks]")
{
  std::vector<unsigned char> const raw{ 0x00, 0x7F, 0x80, 0xFF, 0x01 };
  std::vector<std::byte> flattened;
  for (auto block : raw | dacoexec::views::blocks(3)) {
    flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
  }
  REQUIRE(flattened.size() == raw.size());
  for (std::size_t index = 0; index < raw.size(); ++index) {
    REQUIRE(flattened.at(index) == static_cast<std::byte>(raw.at(index)));
  }

  std::vector<char> chars{ static_cast<char>(-1) };
  auto view = chars | dacoexec::views::blocks(1);
  auto block = std::move(*view.begin());
  REQUIRE(block.bytes_.size() == 1);
  REQUIRE(block.bytes_.at(0) == std::byte{ 0xFF });
}

TEST_CASE("blocks reads a move-only single-pass source once", "[blocks]")
{
  auto stats = std::make_shared<source_stats>();
  constexpr std::size_t count = 10;
  byte_source source{ make_bytes(count), stats };
  std::vector<std::byte> flattened;
  std::uint64_t sequence = 0;
  for (auto block : std::move(source) | dacoexec::views::blocks(3)) {
    REQUIRE(block.sequence_ == sequence);
    ++sequence;
    flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
  }
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(flattened == make_bytes(count));
  REQUIRE(sequence == 4);
}

TEST_CASE("constructing a block view reads nothing and each step reads one block", "[blocks]")
{
  auto stats = std::make_shared<source_stats>();
  constexpr std::size_t count = 32;
  constexpr std::size_t block_size = 4;
  byte_source source{ make_bytes(count), stats };
  auto view = std::move(source) | dacoexec::views::blocks(block_size);

  REQUIRE(stats->begin_calls_ == 0);
  REQUIRE(stats->dereferences_ == 0);
  REQUIRE(stats->increments_ == 0);

  auto cursor = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == block_size);
  REQUIRE(stats->increments_ == block_size);
  REQUIRE((*cursor).sequence_ == 0);

  auto again = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == block_size);
  REQUIRE((*again).sequence_ == 0);

  ++cursor;
  REQUIRE((*cursor).sequence_ == 1);
  REQUIRE(stats->dereferences_ == block_size * 2);
  REQUIRE(stats->dereferences_ < count);

  auto copy = cursor;
  ++cursor;
  REQUIRE((*copy).sequence_ == 2);
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == block_size * 3);
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("moved blocks outlive the source and the view", "[blocks]")
{
  std::vector<dacoexec::byte_block> collected;
  {
    std::vector<std::byte> input = make_bytes(multi_block_input_size);
    {
      auto view = input | dacoexec::views::blocks(4);
      for (auto block : view) { collected.push_back(std::move(block)); }
    }
  }

  REQUIRE(collected.size() == 3);
  REQUIRE(collected.at(0).sequence_ == 0);
  REQUIRE(collected.at(1).sequence_ == 1);
  REQUIRE(collected.at(2).sequence_ == 2);
  REQUIRE(collected.at(0).bytes_.size() == 4);
  REQUIRE(collected.at(1).bytes_.size() == 4);
  REQUIRE(collected.at(2).bytes_.size() == 2);
  REQUIRE(flatten(collected) == make_bytes(10));
}

TEST_CASE("a moved-out block stays intact when the iterator advances", "[blocks]")
{
  std::vector<std::byte> input = make_bytes(two_block_input_size);
  auto view = input | dacoexec::views::blocks(4);
  auto cursor = view.begin();

  auto block = std::move(*cursor);
  auto const *const data = block.bytes_.data();
  auto const size = block.bytes_.size();
  auto const sequence = block.sequence_;
  std::vector<std::byte> snapshot = block.bytes_;

  auto ignored = std::move(*cursor);
  REQUIRE(block.bytes_ == snapshot);
  REQUIRE(block.sequence_ == sequence);
  REQUIRE(block.bytes_.data() == data);
  REQUIRE(block.bytes_.size() == size);
  (void)ignored;

  ++cursor;
  REQUIRE(block.bytes_ == snapshot);
  REQUIRE(block.bytes_.data() == data);
  REQUIRE((*cursor).sequence_ == 1);
  REQUIRE((*cursor).bytes_.size() == 4);
}

TEST_CASE("move assignment transfers an unstarted owning view", "[blocks]")
{
  auto source = make_bytes(short_input_size) | dacoexec::views::blocks(4);
  auto destination = make_bytes(1) | dacoexec::views::blocks(1);
  destination = std::move(source);

  std::size_t from_source = 0;
  // Verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  for (auto block : source) { from_source += block.bytes_.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : destination) { flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end()); }
  REQUIRE(flattened == make_bytes(5));
}

TEST_CASE("moving an unstarted view keeps the unread source", "[blocks]")
{
  std::vector<std::byte> input = make_bytes(short_input_size);
  auto view = input | dacoexec::views::blocks(4);
  auto moved = std::move(view);

  std::size_t from_source = 0;
  // Verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  for (auto block : view) { from_source += block.bytes_.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : moved) { flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end()); }
  REQUIRE(flattened == make_bytes(5));
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("moving a started view keeps the staged block and the unread tail", "[blocks]")
{
  std::vector<std::byte> const expected = make_bytes(10);
  std::vector<std::byte> input = expected;
  auto view = input | dacoexec::views::blocks(4);
  auto cursor = view.begin();
  REQUIRE((*cursor).sequence_ == 0);
  REQUIRE((*cursor).bytes_.size() == 4);

  auto moved = std::move(view);
  std::size_t from_source = 0;
  // Verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  for (auto block : view) { from_source += block.bytes_.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : moved) { flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end()); }
  REQUIRE(flattened == expected);

  auto tail_view = input | dacoexec::views::blocks(4);
  auto tail_cursor = tail_view.begin();
  auto first = std::move(*tail_cursor);
  std::vector<std::byte> const first_bytes = first.bytes_;
  auto tail_moved = std::move(tail_view);
  ++tail_cursor;
  std::vector<std::byte> tail;
  while (tail_cursor != tail_moved.end()) {
    auto block = std::move(*tail_cursor);
    tail.insert(tail.end(), block.bytes_.begin(), block.bytes_.end());
    ++tail_cursor;
  }
  REQUIRE(first.bytes_ == first_bytes);
  REQUIRE(first_bytes.size() == 4);
  REQUIRE(tail.size() == 6);
  std::vector<std::byte> reconstructed = first_bytes;
  reconstructed.insert(reconstructed.end(), tail.begin(), tail.end());
  REQUIRE(reconstructed == expected);
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("zero block size is rejected and a short input does not reserve the whole block", "[blocks]")
{
  auto stats = std::make_shared<source_stats>();
  byte_source const source{ make_bytes(4), stats };
  REQUIRE_THROWS_AS(dacoexec::views::blocks(0), std::invalid_argument);
  REQUIRE(stats->dereferences_ == 0);
  REQUIRE(stats->begin_calls_ == 0);

  std::vector<std::byte> input = make_bytes(1);
  REQUIRE_THROWS_AS(dacoexec::blocks_view(std::views::all(input), std::size_t{ 0 }), std::invalid_argument);

  constexpr std::size_t short_size = 100;
  std::vector<std::byte> short_input = make_bytes(short_size);
  std::vector<dacoexec::byte_block> huge;
  for (auto block : short_input | dacoexec::views::blocks(std::numeric_limits<std::size_t>::max())) {
    huge.push_back(std::move(block));
  }
  REQUIRE(huge.size() == 1);
  REQUIRE(huge.at(0).bytes_.size() == short_size);
  REQUIRE(huge.at(0).bytes_ == short_input);

  constexpr std::size_t wide_size = 10000;
  constexpr std::size_t partial = 5000;
  std::vector<std::byte> wide_input = make_bytes(partial);
  auto wide = std::move(*(wide_input | dacoexec::views::blocks(wide_size)).begin());
  REQUIRE(wide.sequence_ == 0);
  REQUIRE(wide.bytes_.size() == partial);
  REQUIRE(wide.bytes_ == wide_input);
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("upstream failure discards a partial block and stays failed", "[blocks]")
{
  constexpr std::size_t block_size = 4;

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = 1;
    byte_source source{ make_bytes(two_block_input_size), stats };
    auto view = std::move(source) | dacoexec::views::blocks(block_size);
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&]() -> void {
        for (auto block : view) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.empty());

    std::size_t extra = 0;
    for (auto block : view) {
      ++extra;
      (void)block;
    }
    REQUIRE(extra == 0);
  }

  {
    auto stats = std::make_shared<source_stats>();
    // First block consumes 4 reads. The next read fails inside the second block.
    stats->fail_at_dereference_ = block_size + 1U;
    byte_source source{ make_bytes(two_block_input_size), stats };
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&]() -> void {
        for (auto block : std::move(source) | dacoexec::views::blocks(block_size)) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept.at(0).sequence_ == 0);
    REQUIRE(kept.at(0).bytes_.size() == block_size);
    REQUIRE(kept.at(0).bytes_ == make_bytes(block_size));
  }

  {
    auto stats = std::make_shared<source_stats>();
    // One byte of the second block is read, then the source fails.
    stats->fail_at_dereference_ = second_block_failure_read;
    byte_source source{ make_bytes(two_block_input_size), stats };
    auto view = std::move(source) | dacoexec::views::blocks(block_size);
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&]() -> void {
        for (auto block : view) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept.at(0).bytes_.size() == block_size);

    std::size_t extra = 0;
    for (auto block : view) {
      ++extra;
      (void)block;
    }
    REQUIRE(extra == 0);
  }
}

TEST_CASE("exact multiples end cleanly without an empty block", "[blocks]")
{
  auto stats = std::make_shared<source_stats>();
  constexpr std::size_t count = 8;
  constexpr std::size_t block_size = 4;
  byte_source source{ make_bytes(count), stats };
  std::size_t blocks = 0;
  for (auto block : std::move(source) | dacoexec::views::blocks(block_size)) {
    REQUIRE(block.bytes_.size() == block_size);
    ++blocks;
  }
  REQUIRE(blocks == 2);
  REQUIRE(stats->dereferences_ == count);
  REQUIRE(stats->increments_ == count);
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("streaming keeps one block of storage and does not read ahead", "[blocks]")
{
  constexpr std::size_t block_size = 4096;
  constexpr std::size_t blocks = 32;
  constexpr std::size_t count = block_size * blocks;
  auto stats = std::make_shared<source_stats>();
  byte_source source{ make_bytes(count), stats };
  auto view = std::move(source) | dacoexec::views::blocks(block_size);

  REQUIRE(stats->dereferences_ == 0);

  std::size_t seen = 0;
  std::size_t index = 0;
  for (auto block : view) {
    seen += block.bytes_.size();
    REQUIRE(block.sequence_ == index);
    REQUIRE(block.bytes_.size() == block_size);
    REQUIRE(stats->dereferences_ == seen);
    REQUIRE(stats->increments_ == seen);
    ++index;
  }
  REQUIRE(index == blocks);
  REQUIRE(seen == count);
  REQUIRE(stats->begin_calls_ == 1);
}

using codeword_range = decltype(std::declval<std::vector<std::byte> &>()
  | dacoexec::huffman::views::codewords(std::declval<dacoexec::huffman::canonical_codebook>()));
static_assert(std::copyable<dacoexec::huffman::canonical_codebook>);
static_assert(std::movable<dacoexec::huffman::canonical_codebook>);
static_assert(std::same_as<decltype(std::declval<dacoexec::huffman::canonical_codebook &>().lengths()),
  dacoexec::huffman::code_length_table const &>);
static_assert(std::ranges::view<codeword_range>);
static_assert(std::ranges::input_range<codeword_range>);
static_assert(!std::ranges::forward_range<codeword_range>);
static_assert(!std::ranges::borrowed_range<codeword_range>);
static_assert(std::movable<codeword_range>);
static_assert(!std::copyable<codeword_range>);
static_assert(std::same_as<std::ranges::range_reference_t<codeword_range>, dacoexec::huffman::codeword>);

template<class R>
concept can_adapt_codewords = requires(R &&input, dacoexec::huffman::canonical_codebook book) {
  std::forward<R>(input) | dacoexec::huffman::views::codewords(book);
};
static_assert(can_adapt_codewords<std::vector<std::byte> &>);
static_assert(can_adapt_codewords<std::vector<char> &>);
static_assert(can_adapt_codewords<std::vector<signed char> &>);
static_assert(can_adapt_codewords<std::vector<unsigned char> &>);
static_assert(can_adapt_codewords<std::vector<char8_t> &>);
static_assert(can_adapt_codewords<byte_source>);
static_assert(!can_adapt_codewords<byte_source &>);
static_assert(!can_adapt_codewords<std::vector<int> &>);
static_assert(!can_adapt_codewords<std::vector<bool> &>);
static_assert(!can_adapt_codewords<std::vector<std::uint16_t> &>);

TEST_CASE("codewords stages once and preserves single-pass state across moves", "[huffman][codewords]")
{
  auto const input = make_bytes(3);
  auto const lengths = dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(input));
  auto const expected = dacoexec::huffman::make_canonical_codebook(lengths);
  auto stats = std::make_shared<source_stats>();
  auto view = byte_source{ input, stats } | dacoexec::huffman::views::codewords(dacoexec::huffman::make_canonical_codebook(lengths));
  REQUIRE(stats->begin_calls_ == 0);
  REQUIRE(stats->dereferences_ == 0);
  REQUIRE(stats->increments_ == 0);
  auto cursor = view.begin();
  REQUIRE(*cursor == test_detail::canonical_word(expected, 0));
  REQUIRE(*cursor == test_detail::canonical_word(expected, 0));
  REQUIRE(stats->dereferences_ == 1);
  auto alias = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  ++cursor;
  REQUIRE(*alias == test_detail::canonical_word(expected, 1));
  REQUIRE(stats->increments_ == 1);
  REQUIRE(stats->dereferences_ == 2);
  auto moved = std::move(view);
  // Verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  REQUIRE(view.begin() == view.end());
  REQUIRE(*moved.begin() == test_detail::canonical_word(expected, 1));
  ++cursor;
  REQUIRE(*cursor == test_detail::canonical_word(expected, 2));
  REQUIRE(*alias == test_detail::canonical_word(expected, 2));
  ++cursor;
  REQUIRE(cursor == moved.end());
  REQUIRE(stats->increments_ == input.size());
  REQUIRE(stats->dereferences_ == input.size());
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("codewords closures own independent codebooks and accept proxy bytes", "[huffman][codewords]")
{
  auto const input = make_bytes(3);
  auto const book = dacoexec::huffman::make_canonical_codebook(dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(input)));
  auto closure = dacoexec::huffman::views::codewords(book);
  auto copy = closure;
  auto first = closure(input);
  auto second = proxy_bytes{ input } | copy;
  auto left = first.begin();
  auto right = second.begin();
  for (auto const value : input) {
    auto const expected = test_detail::canonical_word(book, std::to_integer<std::uint8_t>(value));
    REQUIRE(*left == expected);
    REQUIRE(*right == expected);
    ++left;
    ++right;
  }
  REQUIRE(left == first.end());
  REQUIRE(right == second.end());
  auto destination = closure(input);
  auto source = closure(input);
  auto cursor = source.begin();
  ++cursor;
  destination = std::move(source);
  // Verify the documented moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  REQUIRE(source.begin() == source.end());
  REQUIRE(*cursor == test_detail::canonical_word(book, 1));
  REQUIRE(*destination.begin() == test_detail::canonical_word(book, 1));
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("codewords failures are terminal without emitting absent symbols", "[huffman][codewords]")
{
  auto const input = make_bytes(3);
  auto const book = dacoexec::huffman::make_canonical_codebook(dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(input)));
  for (std::size_t failure_read : { std::size_t{ 1 }, std::size_t{ 2 } }) {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = failure_read;
    auto view = byte_source{ input, stats } | dacoexec::huffman::views::codewords(book);
    if (failure_read == 1) {
      REQUIRE_THROWS_AS(view.begin(), std::ios_base::failure);
    } else {
      auto cursor = view.begin();
      REQUIRE_THROWS_AS(++cursor, std::ios_base::failure);
      REQUIRE(cursor == view.end());
      ++cursor;
    }
    auto const reads = stats->dereferences_;
    REQUIRE(view.begin() == view.end());
    REQUIRE(stats->dereferences_ == reads);
  }
  std::vector<std::byte> const absent{ std::byte{ 0xFF } };
  auto missing_first = absent | dacoexec::huffman::views::codewords(book);
  REQUIRE_THROWS_AS(missing_first.begin(), std::invalid_argument);
  REQUIRE(missing_first.begin() == missing_first.end());
  std::vector<std::byte> const tail{ input.front(), absent.front() };
  auto missing_tail = tail | dacoexec::huffman::views::codewords(book);
  auto cursor = missing_tail.begin();
  REQUIRE(*cursor == test_detail::canonical_word(book, 0));
  REQUIRE_THROWS_AS(++cursor, std::invalid_argument);
  REQUIRE(cursor == missing_tail.end());
  REQUIRE(missing_tail.begin() == missing_tail.end());
  auto empty = std::vector<std::byte>{} | dacoexec::huffman::views::codewords(dacoexec::huffman::make_canonical_codebook({}));
  REQUIRE(empty.begin() == empty.end());
}

// Catch2 assertions expand into control flow counted by this check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("canonical byte blocks encode the golden bytes without intermediate bits", "[huffman][codewords]")
{
  std::vector<char> const input{ 'A', 'A', 'A', 'A', 'A', 'B', 'B', 'C', 'D' };
  constexpr unsigned expected_padding = 1;
  std::vector<std::byte> const expected{ std::byte{ 0x05 }, std::byte{ 0x6E } };
  for (auto block : input | dacoexec::views::blocks(input.size())) {
    auto const lengths = dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(block.bytes_));
    auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
    auto const saved = book.lengths();
    for (auto const &candidate : { book, dacoexec::huffman::make_canonical_codebook(saved) }) {
      std::vector<std::byte> output;
      dacoexec::bit_writer writer{ std::back_inserter(output) };
      for (auto const word : block.bytes_ | dacoexec::huffman::views::codewords(candidate)) {
        writer.write_bits(word.bits_, word.length_);
      }
      REQUIRE(writer.finish() == expected_padding);
      REQUIRE(output == expected);
      REQUIRE(writer.finish() == expected_padding);
      REQUIRE(output == expected);
    }
  }
}


TEST_CASE("codewords source begin and increment failures permanently end iteration", "[huffman][codewords]")
{
  auto const input = make_bytes(3);
  auto const book = dacoexec::huffman::make_canonical_codebook(dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(input)));

  SECTION("begin failure")
  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_begin_ = true;
    auto view = byte_source{ input, stats } | dacoexec::huffman::views::codewords(book);
    REQUIRE_THROWS_AS(view.begin(), std::ios_base::failure);
    stats->fail_at_begin_ = false;
    REQUIRE(view.begin() == view.end());
    REQUIRE(stats->begin_calls_ == 1);
    REQUIRE(stats->dereferences_ == 0);
    REQUIRE(stats->increments_ == 0);
  }

  SECTION("increment failure")
  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_increment_ = 1;
    auto view = byte_source{ input, stats } | dacoexec::huffman::views::codewords(book);
    auto cursor = view.begin();
    auto alias = cursor;
    REQUIRE(*cursor == test_detail::canonical_word(book, 0));
    REQUIRE_THROWS_AS(++cursor, std::ios_base::failure);
    REQUIRE(cursor == view.end());
    REQUIRE(alias == view.end());
    stats->fail_at_increment_.reset();
    ++cursor;
    REQUIRE(view.begin() == view.end());
    REQUIRE(stats->begin_calls_ == 1);
    REQUIRE(stats->dereferences_ == 1);
    REQUIRE(stats->increments_ == 1);
  }
}

TEST_CASE("codewords preserves negative signed character byte values", "[huffman][codewords]")
{
  constexpr unsigned char high_bit = 0x80;
  constexpr unsigned char all_bits = 0xFF;
  std::array<signed char, 2> const input{
    std::bit_cast<signed char>(high_bit), std::bit_cast<signed char>(all_bits)
  };
  REQUIRE(input.front() < 0);
  REQUIRE(input.back() < 0);
  auto const book = dacoexec::huffman::make_canonical_codebook(dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(input)));
  auto view = input | dacoexec::huffman::views::codewords(book);
  auto cursor = view.begin();
  REQUIRE(*cursor == dacoexec::huffman::codeword{ .bits_ = 0U, .length_ = 1 });
  REQUIRE(*cursor == test_detail::canonical_word(book, high_bit));
  ++cursor;
  REQUIRE(*cursor == dacoexec::huffman::codeword{ .bits_ = 1U, .length_ = 1 });
  REQUIRE(*cursor == test_detail::canonical_word(book, all_bits));
  ++cursor;
  REQUIRE(cursor == view.end());
}
