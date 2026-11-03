#include <catch2/catch_test_macros.hpp>

#include <dacoexec/dacoexec.hpp>

#include <algorithm>
#include <array>
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
#include <utility>
#include <vector>

namespace {

struct source_stats
{
  std::size_t begin_calls_ = 0;
  std::size_t dereferences_ = 0;
  std::size_t increments_ = 0;
  std::optional<std::size_t> fail_at_dereference_;
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
  byte_source &operator=(byte_source const &) = delete;
  byte_source(byte_source &&) = default;
  byte_source &operator=(byte_source &&) = default;
  ~byte_source() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    iterator(iterator const &) = delete;
    iterator &operator=(iterator const &) = delete;
    iterator(iterator &&) = default;
    iterator &operator=(iterator &&) = default;
    ~iterator() = default;

    [[nodiscard]] std::byte operator*() const
    {
      ++parent_->stats_->dereferences_;
      if (parent_->stats_->fail_at_dereference_.has_value()
          && parent_->stats_->dereferences_ >= *parent_->stats_->fail_at_dereference_) {
        throw std::ios_base::failure("injected read failure");
      }
      return parent_->data_.at(parent_->cursor_);
    }

    iterator &operator++()
    {
      ++parent_->stats_->increments_;
      ++parent_->cursor_;
      return *this;
    }

    void operator++(int) { ++*this; }

    // A member, not a hidden friend: GCC 12 does not let a friend defined in
    // this nested iterator reach byte_source's private state.
    [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept
    { return parent_->cursor_ == parent_->data_.size(); }

  private:
    friend class byte_source;

    explicit iterator(byte_source *parent) : parent_(parent) {}

    byte_source *parent_ = nullptr;
  };

  [[nodiscard]] auto begin() -> iterator
  {
    ++stats_->begin_calls_;
    return iterator{ this };
  }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

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
static_assert(dacoexec::detail::next_block_capacity(0, std::numeric_limits<std::size_t>::max()) == 4096);
static_assert(dacoexec::detail::next_block_capacity(4096, 10000) == 8192);
static_assert(dacoexec::detail::next_block_capacity(8192, 10000) == 10000);
static_assert(dacoexec::detail::next_block_capacity((std::numeric_limits<std::size_t>::max() / 2) + 1,
                std::numeric_limits<std::size_t>::max())
              == std::numeric_limits<std::size_t>::max());

[[nodiscard]] std::vector<std::byte> make_bytes(std::size_t count)
{
  std::vector<std::byte> bytes(count);
  for (std::size_t index = 0; index < count; ++index) {
    bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(index));
  }
  return bytes;
}

[[nodiscard]] std::vector<std::byte> flatten(std::vector<dacoexec::byte_block> const &blocks)
{
  std::vector<std::byte> out;
  for (dacoexec::byte_block const &block : blocks) { out.insert(out.end(), block.bytes_.begin(), block.bytes_.end()); }
  return out;
}

void check_partition(std::size_t count, std::size_t block_size, std::span<std::size_t const> expected_sizes)
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
  REQUIRE(std::equal(sizes.begin(), sizes.end(), expected_sizes.begin(), expected_sizes.end()));
  REQUIRE(flattened == expected);
}

}// namespace

// * returns a proxy. value_type is still std::byte, so the range models
// byte_input_range. common_reference is specialized so the proxy satisfies
// indirectly_readable.
struct byte_proxy
{
  std::byte value_{};
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

    [[nodiscard]] byte_proxy operator*() const { return byte_proxy{ *ptr_ }; }

    iterator &operator++()
    {
      ++ptr_;
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(iterator const &it, std::default_sentinel_t) noexcept
    { return it.ptr_ == it.last_; }

  private:
    friend class proxy_bytes;

    iterator(std::byte const *ptr, std::byte const *last) : ptr_(ptr), last_(last) {}

    std::byte const *ptr_ = nullptr;
    std::byte const *last_ = nullptr;
  };

  [[nodiscard]] auto begin() -> iterator { return iterator{ data_.data(), data_.data() + data_.size() }; }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

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
  check_partition(5, 4, five);
  check_partition(8, 4, eight);
  check_partition(9, 4, nine);
  check_partition(7, 1, one);
  check_partition(10, 3, ten);
}

TEST_CASE("blocks accepts standard ranges, call syntax, and owning rvalues", "[blocks]")
{
  std::vector<std::byte> const expected = make_bytes(5);

  {
    std::vector<std::byte> input = expected;
    std::vector<dacoexec::byte_block> blocks;
    for (auto block : dacoexec::views::blocks(4)(input)) { blocks.push_back(std::move(block)); }
    REQUIRE(blocks.size() == 2);
    REQUIRE(blocks[0].bytes_.size() == 4);
    REQUIRE(blocks[1].bytes_.size() == 1);
    REQUIRE(flatten(blocks) == expected);
  }

  {
    std::array<std::byte, 5> input{};
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
    std::span<std::byte const> input{ storage };
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
    REQUIRE(flattened[index] == static_cast<std::byte>(raw[index]));
  }

  std::vector<char> chars{ static_cast<char>(-1) };
  auto view = chars | dacoexec::views::blocks(1);
  auto block = std::move(*view.begin());
  REQUIRE(block.bytes_.size() == 1);
  REQUIRE(block.bytes_[0] == std::byte{ 0xFF });
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

  auto it = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == block_size);
  REQUIRE(stats->increments_ == block_size);
  REQUIRE((*it).sequence_ == 0);

  auto again = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == block_size);
  REQUIRE((*again).sequence_ == 0);

  ++it;
  REQUIRE((*it).sequence_ == 1);
  REQUIRE(stats->dereferences_ == block_size * 2);
  REQUIRE(stats->dereferences_ < count);

  auto copy = it;
  ++it;
  REQUIRE((*copy).sequence_ == 2);
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == block_size * 3);
}

TEST_CASE("moved blocks outlive the source and the view", "[blocks]")
{
  std::vector<dacoexec::byte_block> collected;
  {
    std::vector<std::byte> input = make_bytes(10);
    {
      auto view = input | dacoexec::views::blocks(4);
      for (auto block : view) { collected.push_back(std::move(block)); }
    }
  }

  REQUIRE(collected.size() == 3);
  REQUIRE(collected[0].sequence_ == 0);
  REQUIRE(collected[1].sequence_ == 1);
  REQUIRE(collected[2].sequence_ == 2);
  REQUIRE(collected[0].bytes_.size() == 4);
  REQUIRE(collected[1].bytes_.size() == 4);
  REQUIRE(collected[2].bytes_.size() == 2);
  REQUIRE(flatten(collected) == make_bytes(10));
}

TEST_CASE("a moved-out block stays intact when the iterator advances", "[blocks]")
{
  std::vector<std::byte> input = make_bytes(8);
  auto view = input | dacoexec::views::blocks(4);
  auto it = view.begin();

  auto block = std::move(*it);
  auto const *const data = block.bytes_.data();
  auto const size = block.bytes_.size();
  auto const sequence = block.sequence_;
  std::vector<std::byte> snapshot = block.bytes_;

  auto ignored = std::move(*it);
  REQUIRE(block.bytes_ == snapshot);
  REQUIRE(block.sequence_ == sequence);
  REQUIRE(block.bytes_.data() == data);
  REQUIRE(block.bytes_.size() == size);
  (void)ignored;

  ++it;
  REQUIRE(block.bytes_ == snapshot);
  REQUIRE(block.bytes_.data() == data);
  REQUIRE((*it).sequence_ == 1);
  REQUIRE((*it).bytes_.size() == 4);
}

TEST_CASE("move assignment transfers an unstarted owning view", "[blocks]")
{
  auto source = make_bytes(5) | dacoexec::views::blocks(4);
  auto destination = make_bytes(1) | dacoexec::views::blocks(1);
  destination = std::move(source);

  std::size_t from_source = 0;
  for (auto block : source) { from_source += block.bytes_.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : destination) { flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end()); }
  REQUIRE(flattened == make_bytes(5));
}

TEST_CASE("moving an unstarted view keeps the unread source", "[blocks]")
{
  std::vector<std::byte> input = make_bytes(5);
  auto view = input | dacoexec::views::blocks(4);
  auto moved = std::move(view);

  std::size_t from_source = 0;
  for (auto block : view) { from_source += block.bytes_.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : moved) { flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end()); }
  REQUIRE(flattened == make_bytes(5));
}

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

TEST_CASE("zero block size is rejected and a short input does not reserve the whole block", "[blocks]")
{
  auto stats = std::make_shared<source_stats>();
  byte_source source{ make_bytes(4), stats };
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
  REQUIRE(huge[0].bytes_.size() == short_size);
  REQUIRE(huge[0].bytes_ == short_input);

  constexpr std::size_t wide_size = 10000;
  constexpr std::size_t partial = 5000;
  std::vector<std::byte> wide_input = make_bytes(partial);
  auto wide = std::move(*(wide_input | dacoexec::views::blocks(wide_size)).begin());
  REQUIRE(wide.sequence_ == 0);
  REQUIRE(wide.bytes_.size() == partial);
  REQUIRE(wide.bytes_ == wide_input);
}

TEST_CASE("upstream failure discards a partial block and stays failed", "[blocks]")
{
  constexpr std::size_t block_size = 4;

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = 1;
    byte_source source{ make_bytes(8), stats };
    auto view = std::move(source) | dacoexec::views::blocks(block_size);
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&] {
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
    stats->fail_at_dereference_ = 5;
    byte_source source{ make_bytes(8), stats };
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (auto block : std::move(source) | dacoexec::views::blocks(block_size)) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].sequence_ == 0);
    REQUIRE(kept[0].bytes_.size() == block_size);
    REQUIRE(kept[0].bytes_ == make_bytes(block_size));
  }

  {
    auto stats = std::make_shared<source_stats>();
    // One byte of the second block is read, then the source fails.
    stats->fail_at_dereference_ = 6;
    byte_source source{ make_bytes(8), stats };
    auto view = std::move(source) | dacoexec::views::blocks(block_size);
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (auto block : view) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].bytes_.size() == block_size);

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
