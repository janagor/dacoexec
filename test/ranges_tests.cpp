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

struct SourceStats
{
  std::size_t begin_calls = 0;
  std::size_t dereferences = 0;
  std::size_t increments = 0;
  std::optional<std::size_t> fail_at_dereference;
};

// Move-only, single-pass byte source. begin() does not rewind: every iterator
// shares one cursor stored in the source, and the cursor only advances.
class ByteSource : public std::ranges::view_interface<ByteSource>
{
public:
  ByteSource(std::vector<std::byte> data, std::shared_ptr<SourceStats> stats)
    : data_(std::move(data)), stats_(std::move(stats))
  {}

  ByteSource(ByteSource const &) = delete;
  ByteSource &operator=(ByteSource const &) = delete;
  ByteSource(ByteSource &&) = default;
  ByteSource &operator=(ByteSource &&) = default;
  ~ByteSource() = default;

  class Iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    Iterator(Iterator const &) = delete;
    Iterator &operator=(Iterator const &) = delete;
    Iterator(Iterator &&) = default;
    Iterator &operator=(Iterator &&) = default;
    ~Iterator() = default;

    [[nodiscard]] std::byte operator*() const
    {
      ++parent_->stats_->dereferences;
      if (parent_->stats_->fail_at_dereference.has_value()
          && parent_->stats_->dereferences >= *parent_->stats_->fail_at_dereference) {
        throw std::ios_base::failure("injected read failure");
      }
      return parent_->data_.at(parent_->cursor_);
    }

    Iterator &operator++()
    {
      ++parent_->stats_->increments;
      ++parent_->cursor_;
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(Iterator const &it, std::default_sentinel_t) noexcept
    { return it.parent_->cursor_ == it.parent_->data_.size(); }

  private:
    friend class ByteSource;

    explicit Iterator(ByteSource *parent) : parent_(parent) {}

    ByteSource *parent_ = nullptr;
  };

  [[nodiscard]] auto begin() -> Iterator
  {
    ++stats_->begin_calls;
    return Iterator{ this };
  }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
  std::shared_ptr<SourceStats> stats_;
  // Retained so a second begin() cannot rewind a consumed source. The iterator
  // itself is move-only and carries the cursor for the single pass.
  std::size_t cursor_ = 0;
};

static_assert(std::ranges::view<ByteSource>);
static_assert(std::ranges::input_range<ByteSource>);
static_assert(!std::ranges::forward_range<ByteSource>);
static_assert(!std::ranges::sized_range<ByteSource>);
static_assert(!std::copyable<ByteSource>);
static_assert(dacoexec::byte_input_range<ByteSource>);
static_assert(dacoexec::byte_input_range<std::vector<std::byte>>);
static_assert(dacoexec::byte_input_range<std::array<std::byte, 1>>);
static_assert(dacoexec::byte_input_range<std::span<std::byte const>>);
static_assert(dacoexec::byte_input_range<std::vector<unsigned char>>);
static_assert(dacoexec::byte_input_range<std::vector<char>>);
static_assert(dacoexec::byte_input_range<std::vector<signed char>>);
static_assert(dacoexec::byte_input_range<std::vector<char8_t>>);
static_assert(!dacoexec::byte_input_range<std::vector<int>>);

template<class R>
concept CanPipeBlocks = requires(R &&range) { std::forward<R>(range) | dacoexec::views::blocks(std::size_t{ 4 }); };

static_assert(CanPipeBlocks<std::vector<std::byte> &>);
static_assert(CanPipeBlocks<std::vector<std::byte>>);
static_assert(CanPipeBlocks<ByteSource>);
static_assert(!CanPipeBlocks<ByteSource &>);
static_assert(!CanPipeBlocks<std::vector<int> &>);

using BlockRange = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::blocks(std::size_t{ 4 }));

static_assert(std::ranges::view<BlockRange>);
static_assert(std::ranges::input_range<BlockRange>);
static_assert(!std::ranges::forward_range<BlockRange>);
static_assert(!std::ranges::borrowed_range<BlockRange>);
static_assert(std::same_as<std::ranges::range_value_t<BlockRange>, dacoexec::byte_block>);
static_assert(std::same_as<std::iter_reference_t<std::ranges::iterator_t<BlockRange>>, dacoexec::byte_block &&>);
static_assert(std::input_iterator<std::ranges::iterator_t<BlockRange>>);
static_assert(std::movable<dacoexec::byte_block>);
static_assert(!std::copyable<dacoexec::byte_block>);
static_assert(std::is_nothrow_move_constructible_v<dacoexec::byte_block>);
static_assert(std::is_nothrow_move_assignable_v<dacoexec::byte_block>);
static_assert(std::movable<BlockRange>);
static_assert(!std::copyable<BlockRange>);
static_assert(std::same_as<decltype(dacoexec::byte_block::sequence), std::uint64_t>);
static_assert(dacoexec::detail::NextBlockCapacity(0, std::numeric_limits<std::size_t>::max()) == 4096);
static_assert(dacoexec::detail::NextBlockCapacity(4096, 10000) == 8192);
static_assert(dacoexec::detail::NextBlockCapacity(8192, 10000) == 10000);
static_assert(dacoexec::detail::NextBlockCapacity((std::numeric_limits<std::size_t>::max() / 2) + 1,
                std::numeric_limits<std::size_t>::max())
              == std::numeric_limits<std::size_t>::max());

[[nodiscard]] std::vector<std::byte> MakeBytes(std::size_t count)
{
  std::vector<std::byte> bytes(count);
  for (std::size_t index = 0; index < count; ++index) {
    bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(index));
  }
  return bytes;
}

[[nodiscard]] std::vector<std::byte> Flatten(std::vector<dacoexec::byte_block> const &blocks)
{
  std::vector<std::byte> out;
  for (dacoexec::byte_block const &block : blocks) { out.insert(out.end(), block.bytes.begin(), block.bytes.end()); }
  return out;
}

void CheckPartition(std::size_t count, std::size_t block_size, std::span<std::size_t const> expected_sizes)
{
  std::vector<std::byte> const expected = MakeBytes(count);
  std::vector<std::byte> input = expected;
  std::vector<std::size_t> sizes;
  std::vector<std::byte> flattened;
  std::uint64_t sequence = 0;
  for (auto block : input | dacoexec::views::blocks(block_size)) {
    REQUIRE(block.sequence == sequence);
    ++sequence;
    sizes.push_back(block.bytes.size());
    flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
  }
  REQUIRE(sizes.size() == expected_sizes.size());
  REQUIRE(std::equal(sizes.begin(), sizes.end(), expected_sizes.begin(), expected_sizes.end()));
  REQUIRE(flattened == expected);
}

}// namespace

// * returns a proxy. value_type is still std::byte, so the range models
// byte_input_range. common_reference is specialized so the proxy satisfies
// indirectly_readable.
struct ByteProxy
{
  std::byte value{};
  constexpr operator std::byte() const { return value; }
};

template<template<class> class QualByte, template<class> class QualProxy>
struct std::basic_common_reference<std::byte, ByteProxy, QualByte, QualProxy>
{
  using type = std::byte;
};

template<template<class> class QualProxy, template<class> class QualByte>
struct std::basic_common_reference<ByteProxy, std::byte, QualProxy, QualByte>
{
  using type = std::byte;
};

class ProxyBytes : public std::ranges::view_interface<ProxyBytes>
{
public:
  explicit ProxyBytes(std::vector<std::byte> data) : data_(std::move(data)) {}

  class Iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    [[nodiscard]] ByteProxy operator*() const { return ByteProxy{ *ptr_ }; }

    Iterator &operator++()
    {
      ++ptr_;
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(Iterator const &it, std::default_sentinel_t) noexcept
    { return it.ptr_ == it.last_; }

  private:
    friend class ProxyBytes;

    Iterator(std::byte const *ptr, std::byte const *last) : ptr_(ptr), last_(last) {}

    std::byte const *ptr_ = nullptr;
    std::byte const *last_ = nullptr;
  };

  [[nodiscard]] auto begin() -> Iterator { return Iterator{ data_.data(), data_.data() + data_.size() }; }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
};

static_assert(std::input_iterator<ProxyBytes::Iterator>);
static_assert(std::ranges::input_range<ProxyBytes>);
static_assert(dacoexec::byte_input_range<ProxyBytes>);
static_assert(std::same_as<std::ranges::range_value_t<ProxyBytes>, std::byte>);
static_assert(std::same_as<std::iter_reference_t<ProxyBytes::Iterator>, ByteProxy>);

TEST_CASE("blocks reads a proxy-backed byte range", "[blocks]")
{
  std::vector<std::byte> const expected = MakeBytes(10);
  std::vector<std::size_t> sizes;
  std::vector<std::byte> flattened;
  std::uint64_t sequence = 0;
  for (auto block : ProxyBytes{ expected } | dacoexec::views::blocks(4)) {
    REQUIRE(block.sequence == sequence);
    ++sequence;
    sizes.push_back(block.bytes.size());
    flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
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

  CheckPartition(0, 4, {});
  CheckPartition(1, 4, only_one);
  CheckPartition(4, 4, exact);
  CheckPartition(5, 4, five);
  CheckPartition(8, 4, eight);
  CheckPartition(9, 4, nine);
  CheckPartition(7, 1, one);
  CheckPartition(10, 3, ten);
}

TEST_CASE("blocks accepts standard ranges, call syntax, and owning rvalues", "[blocks]")
{
  std::vector<std::byte> const expected = MakeBytes(5);

  {
    std::vector<std::byte> input = expected;
    std::vector<dacoexec::byte_block> blocks;
    for (auto block : dacoexec::views::blocks(4)(input)) { blocks.push_back(std::move(block)); }
    REQUIRE(blocks.size() == 2);
    REQUIRE(blocks[0].bytes.size() == 4);
    REQUIRE(blocks[1].bytes.size() == 1);
    REQUIRE(Flatten(blocks) == expected);
  }

  {
    std::array<std::byte, 5> input{};
    std::ranges::copy(expected, input.begin());
    std::vector<std::byte> flattened;
    std::uint64_t sequence = 0;
    for (auto block : input | dacoexec::views::blocks(4)) {
      REQUIRE(block.sequence == sequence);
      ++sequence;
      flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
    }
    REQUIRE(flattened == expected);
  }

  {
    std::vector<std::byte> storage = expected;
    std::span<std::byte const> input{ storage };
    std::vector<std::byte> flattened;
    for (auto block : input | dacoexec::views::blocks(2)) {
      flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
    }
    REQUIRE(flattened == expected);
  }

  {
    std::vector<dacoexec::byte_block> blocks;
    for (auto block : MakeBytes(5) | dacoexec::views::blocks(4)) { blocks.push_back(std::move(block)); }
    REQUIRE(Flatten(blocks) == expected);
  }
}

TEST_CASE("blocks converts character bytes and preserves high bits", "[blocks]")
{
  std::vector<unsigned char> const raw{ 0x00, 0x7F, 0x80, 0xFF, 0x01 };
  std::vector<std::byte> flattened;
  for (auto block : raw | dacoexec::views::blocks(3)) {
    flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
  }
  REQUIRE(flattened.size() == raw.size());
  for (std::size_t index = 0; index < raw.size(); ++index) {
    REQUIRE(flattened[index] == static_cast<std::byte>(raw[index]));
  }

  std::vector<char> chars{ static_cast<char>(-1) };
  auto view = chars | dacoexec::views::blocks(1);
  auto block = std::move(*view.begin());
  REQUIRE(block.bytes.size() == 1);
  REQUIRE(block.bytes[0] == std::byte{ 0xFF });
}

TEST_CASE("blocks reads a move-only single-pass source once", "[blocks]")
{
  auto stats = std::make_shared<SourceStats>();
  constexpr std::size_t kCount = 10;
  ByteSource source{ MakeBytes(kCount), stats };
  std::vector<std::byte> flattened;
  std::uint64_t sequence = 0;
  for (auto block : std::move(source) | dacoexec::views::blocks(3)) {
    REQUIRE(block.sequence == sequence);
    ++sequence;
    flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
  }
  REQUIRE(stats->begin_calls == 1);
  REQUIRE(flattened == MakeBytes(kCount));
  REQUIRE(sequence == 4);
}

TEST_CASE("constructing a block view reads nothing and each step reads one block", "[blocks]")
{
  auto stats = std::make_shared<SourceStats>();
  constexpr std::size_t kCount = 32;
  constexpr std::size_t kBlock = 4;
  ByteSource source{ MakeBytes(kCount), stats };
  auto view = std::move(source) | dacoexec::views::blocks(kBlock);

  REQUIRE(stats->begin_calls == 0);
  REQUIRE(stats->dereferences == 0);
  REQUIRE(stats->increments == 0);

  auto it = view.begin();
  REQUIRE(stats->begin_calls == 1);
  REQUIRE(stats->dereferences == kBlock);
  REQUIRE(stats->increments == kBlock);
  REQUIRE((*it).sequence == 0);

  auto again = view.begin();
  REQUIRE(stats->begin_calls == 1);
  REQUIRE(stats->dereferences == kBlock);
  REQUIRE((*again).sequence == 0);

  ++it;
  REQUIRE((*it).sequence == 1);
  REQUIRE(stats->dereferences == kBlock * 2);
  REQUIRE(stats->dereferences < kCount);

  auto copy = it;
  ++it;
  REQUIRE((*copy).sequence == 2);
  REQUIRE(stats->begin_calls == 1);
  REQUIRE(stats->dereferences == kBlock * 3);
}

TEST_CASE("moved blocks outlive the source and the view", "[blocks]")
{
  std::vector<dacoexec::byte_block> collected;
  {
    std::vector<std::byte> input = MakeBytes(10);
    {
      auto view = input | dacoexec::views::blocks(4);
      for (auto block : view) { collected.push_back(std::move(block)); }
    }
  }

  REQUIRE(collected.size() == 3);
  REQUIRE(collected[0].sequence == 0);
  REQUIRE(collected[1].sequence == 1);
  REQUIRE(collected[2].sequence == 2);
  REQUIRE(collected[0].bytes.size() == 4);
  REQUIRE(collected[1].bytes.size() == 4);
  REQUIRE(collected[2].bytes.size() == 2);
  REQUIRE(Flatten(collected) == MakeBytes(10));
}

TEST_CASE("a moved-out block stays intact when the iterator advances", "[blocks]")
{
  std::vector<std::byte> input = MakeBytes(8);
  auto view = input | dacoexec::views::blocks(4);
  auto it = view.begin();

  auto block = std::move(*it);
  auto const *const data = block.bytes.data();
  auto const size = block.bytes.size();
  auto const sequence = block.sequence;
  std::vector<std::byte> snapshot = block.bytes;

  auto ignored = std::move(*it);
  REQUIRE(block.bytes == snapshot);
  REQUIRE(block.sequence == sequence);
  REQUIRE(block.bytes.data() == data);
  REQUIRE(block.bytes.size() == size);
  (void)ignored;

  ++it;
  REQUIRE(block.bytes == snapshot);
  REQUIRE(block.bytes.data() == data);
  REQUIRE((*it).sequence == 1);
  REQUIRE((*it).bytes.size() == 4);
}

TEST_CASE("move assignment transfers an unstarted owning view", "[blocks]")
{
  auto source = MakeBytes(5) | dacoexec::views::blocks(4);
  auto destination = MakeBytes(1) | dacoexec::views::blocks(1);
  destination = std::move(source);

  std::size_t from_source = 0;
  for (auto block : source) { from_source += block.bytes.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : destination) { flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end()); }
  REQUIRE(flattened == MakeBytes(5));
}

TEST_CASE("moving an unstarted view keeps the unread source", "[blocks]")
{
  std::vector<std::byte> input = MakeBytes(5);
  auto view = input | dacoexec::views::blocks(4);
  auto moved = std::move(view);

  std::size_t from_source = 0;
  for (auto block : view) { from_source += block.bytes.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : moved) { flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end()); }
  REQUIRE(flattened == MakeBytes(5));
}

TEST_CASE("moving a started view keeps the staged block and the unread tail", "[blocks]")
{
  std::vector<std::byte> const expected = MakeBytes(10);
  std::vector<std::byte> input = expected;
  auto view = input | dacoexec::views::blocks(4);
  auto cursor = view.begin();
  REQUIRE((*cursor).sequence == 0);
  REQUIRE((*cursor).bytes.size() == 4);

  auto moved = std::move(view);
  std::size_t from_source = 0;
  for (auto block : view) { from_source += block.bytes.size(); }
  REQUIRE(from_source == 0);

  std::vector<std::byte> flattened;
  for (auto block : moved) { flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end()); }
  REQUIRE(flattened == expected);

  auto tail_view = input | dacoexec::views::blocks(4);
  auto tail_cursor = tail_view.begin();
  auto first = std::move(*tail_cursor);
  std::vector<std::byte> const first_bytes = first.bytes;
  auto tail_moved = std::move(tail_view);
  ++tail_cursor;
  std::vector<std::byte> tail;
  while (tail_cursor != tail_moved.end()) {
    auto block = std::move(*tail_cursor);
    tail.insert(tail.end(), block.bytes.begin(), block.bytes.end());
    ++tail_cursor;
  }
  REQUIRE(first.bytes == first_bytes);
  REQUIRE(first_bytes.size() == 4);
  REQUIRE(tail.size() == 6);
  std::vector<std::byte> reconstructed = first_bytes;
  reconstructed.insert(reconstructed.end(), tail.begin(), tail.end());
  REQUIRE(reconstructed == expected);
}

TEST_CASE("zero block size is rejected and a short input does not reserve the whole block", "[blocks]")
{
  auto stats = std::make_shared<SourceStats>();
  ByteSource source{ MakeBytes(4), stats };
  REQUIRE_THROWS_AS(dacoexec::views::blocks(0), std::invalid_argument);
  REQUIRE(stats->dereferences == 0);
  REQUIRE(stats->begin_calls == 0);

  std::vector<std::byte> input = MakeBytes(1);
  REQUIRE_THROWS_AS(dacoexec::blocks_view(std::views::all(input), std::size_t{ 0 }), std::invalid_argument);

  constexpr std::size_t kShort = 100;
  std::vector<std::byte> short_input = MakeBytes(kShort);
  std::vector<dacoexec::byte_block> huge;
  for (auto block : short_input | dacoexec::views::blocks(std::numeric_limits<std::size_t>::max())) {
    huge.push_back(std::move(block));
  }
  REQUIRE(huge.size() == 1);
  REQUIRE(huge[0].bytes.size() == kShort);
  REQUIRE(huge[0].bytes == short_input);

  constexpr std::size_t kWide = 10000;
  constexpr std::size_t kPartial = 5000;
  std::vector<std::byte> wide_input = MakeBytes(kPartial);
  auto wide = std::move(*(wide_input | dacoexec::views::blocks(kWide)).begin());
  REQUIRE(wide.sequence == 0);
  REQUIRE(wide.bytes.size() == kPartial);
  REQUIRE(wide.bytes == wide_input);
}

TEST_CASE("upstream failure discards a partial block and stays failed", "[blocks]")
{
  constexpr std::size_t kBlock = 4;

  {
    auto stats = std::make_shared<SourceStats>();
    stats->fail_at_dereference = 1;
    ByteSource source{ MakeBytes(8), stats };
    auto view = std::move(source) | dacoexec::views::blocks(kBlock);
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
    auto stats = std::make_shared<SourceStats>();
    // First block consumes 4 reads. The next read fails inside the second block.
    stats->fail_at_dereference = 5;
    ByteSource source{ MakeBytes(8), stats };
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (auto block : std::move(source) | dacoexec::views::blocks(kBlock)) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].sequence == 0);
    REQUIRE(kept[0].bytes.size() == kBlock);
    REQUIRE(kept[0].bytes == MakeBytes(kBlock));
  }

  {
    auto stats = std::make_shared<SourceStats>();
    // One byte of the second block is read, then the source fails.
    stats->fail_at_dereference = 6;
    ByteSource source{ MakeBytes(8), stats };
    auto view = std::move(source) | dacoexec::views::blocks(kBlock);
    std::vector<dacoexec::byte_block> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (auto block : view) { kept.push_back(std::move(block)); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].bytes.size() == kBlock);

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
  auto stats = std::make_shared<SourceStats>();
  constexpr std::size_t kCount = 8;
  constexpr std::size_t kBlock = 4;
  ByteSource source{ MakeBytes(kCount), stats };
  std::size_t blocks = 0;
  for (auto block : std::move(source) | dacoexec::views::blocks(kBlock)) {
    REQUIRE(block.bytes.size() == kBlock);
    ++blocks;
  }
  REQUIRE(blocks == 2);
  REQUIRE(stats->dereferences == kCount);
  REQUIRE(stats->increments == kCount);
}

TEST_CASE("streaming keeps one block of storage and does not read ahead", "[blocks]")
{
  constexpr std::size_t kBlock = 4096;
  constexpr std::size_t kBlocks = 32;
  constexpr std::size_t kCount = kBlock * kBlocks;
  auto stats = std::make_shared<SourceStats>();
  ByteSource source{ MakeBytes(kCount), stats };
  auto view = std::move(source) | dacoexec::views::blocks(kBlock);

  REQUIRE(stats->dereferences == 0);

  std::size_t seen = 0;
  std::size_t index = 0;
  for (auto block : view) {
    seen += block.bytes.size();
    REQUIRE(block.sequence == index);
    REQUIRE(block.bytes.size() == kBlock);
    REQUIRE(stats->dereferences == seen);
    REQUIRE(stats->increments == seen);
    ++index;
  }
  REQUIRE(index == kBlocks);
  REQUIRE(seen == kCount);
  REQUIRE(stats->begin_calls == 1);
}
