#include <dacoexec/views/bits.hpp>
#include <dacoexec/views/buffered_bytes.hpp>

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/views/blocks.hpp>

#include <catch2/catch_test_macros.hpp>

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
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct source_stats
{
  std::size_t begin_calls_ = 0;
  std::size_t dereferences_ = 0;
  std::size_t increments_ = 0;
  std::optional<std::size_t> fail_at_dereference_;
  std::optional<std::size_t> fail_at_increment_;
  bool fail_begin_ = false;
};

// Move-only, single-pass byte source. begin() does not rewind.
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
      if (parent_->stats_->fail_at_increment_.has_value()
          && parent_->stats_->increments_ >= *parent_->stats_->fail_at_increment_) {
        throw std::ios_base::failure("injected advance failure");
      }
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
    if (stats_->fail_begin_) { throw std::ios_base::failure("injected begin failure"); }
    return iterator{ this };
  }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
  std::shared_ptr<source_stats> stats_;
  std::size_t cursor_ = 0;
};

static_assert(std::ranges::input_range<byte_source>);
static_assert(!std::ranges::forward_range<byte_source>);
static_assert(!std::copyable<byte_source>);
static_assert(dacoexec::byte_input_range<byte_source>);

template<class R>
concept can_pipe_bits = requires(R &&range) { std::forward<R>(range) | dacoexec::views::bits(); };

static_assert(can_pipe_bits<std::vector<std::byte> &>);
static_assert(can_pipe_bits<std::vector<std::byte>>);
static_assert(can_pipe_bits<std::span<std::byte const>>);
static_assert(can_pipe_bits<byte_source>);
static_assert(!can_pipe_bits<byte_source &>);
static_assert(!can_pipe_bits<std::vector<int> &>);
static_assert(!can_pipe_bits<std::vector<std::uint16_t>>);
static_assert(!can_pipe_bits<std::vector<bool> &>);

using bit_range = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::bits());

static_assert(std::ranges::view<bit_range>);
static_assert(std::ranges::input_range<bit_range>);
static_assert(!std::ranges::forward_range<bit_range>);
static_assert(!std::ranges::random_access_range<bit_range>);
static_assert(!std::ranges::sized_range<bit_range>);
static_assert(!std::ranges::borrowed_range<bit_range>);
static_assert(std::same_as<std::ranges::range_value_t<bit_range>, bool>);
static_assert(std::same_as<std::iter_reference_t<std::ranges::iterator_t<bit_range>>, bool>);
static_assert(std::input_iterator<std::ranges::iterator_t<bit_range>>);
static_assert(std::movable<bit_range>);
static_assert(!std::copyable<bit_range>);
static_assert(std::is_nothrow_move_constructible_v<bit_range>);
static_assert(sizeof(bit_range) <= sizeof(void *) * 2);

using byte_insert = std::back_insert_iterator<std::vector<std::byte>>;
using writer = dacoexec::bit_writer<byte_insert>;
using bit_out = dacoexec::bit_output_iterator<byte_insert>;

static_assert(std::output_iterator<bit_out, bool>);
static_assert(std::indirectly_writable<bit_out, bool>);
static_assert(std::copyable<bit_out>);
static_assert(!std::copyable<writer>);
static_assert(std::movable<writer>);
static_assert(std::same_as<decltype(std::declval<writer &>().output_iterator()), bit_out>);

template<class Out>
concept can_make_writer = requires(Out output) { dacoexec::bit_writer<Out>{ std::move(output) }; };

static_assert(can_make_writer<byte_insert>);
static_assert(can_make_writer<std::vector<std::byte>::iterator>);
static_assert(!can_make_writer<std::vector<std::byte>::const_iterator>);
static_assert(!can_make_writer<int>);

[[nodiscard]] std::vector<std::byte> make_bytes(std::size_t count)
{
  std::vector<std::byte> bytes(count);
  for (std::size_t index = 0; index < count; ++index) {
    bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(index));
  }
  return bytes;
}

template<class R> std::vector<int> collect_bits(R &&range)
{
  std::vector<int> bits;
  for (bool bit : std::forward<R>(range)) { bits.push_back(bit ? 1 : 0); }
  return bits;
}

[[nodiscard]] std::vector<int> reference_bits(std::span<std::byte const> bytes)
{
  std::vector<int> bits;
  bits.reserve(bytes.size() * 8U);
  for (std::byte byte : bytes) {
    unsigned const value = std::to_integer<unsigned>(byte);
    for (unsigned shift = 0; shift < 8U; ++shift) { bits.push_back(((value >> (7U - shift)) & 1U) != 0U ? 1 : 0); }
  }
  return bits;
}

struct packed_bytes
{
  std::vector<std::byte> bytes_;
  std::uint8_t padding_ = 0;
};

class reference_writer
{
public:
  void write_bit(bool bit) { bits_.push_back(bit ? std::uint8_t{ 1 } : std::uint8_t{ 0 }); }

  void write_bits(std::uint32_t value, unsigned bit_count)
  {
    for (unsigned remaining = bit_count; remaining > 0U; --remaining) {
      unsigned const shift = remaining - 1U;
      bool const bit = ((value >> shift) & 1U) != 0U;
      write_bit(bit);
    }
  }

  [[nodiscard]] packed_bytes finish() const
  {
    packed_bytes packed;
    std::size_t const count = bits_.size();
    unsigned const remainder = static_cast<unsigned>(count % 8U);
    packed.padding_ = remainder == 0U ? std::uint8_t{ 0 } : static_cast<std::uint8_t>(8U - remainder);
    std::size_t const byte_count = (count / 8U) + (remainder == 0U ? 0U : 1U);
    packed.bytes_.resize(byte_count);
    for (std::size_t index = 0; index < count; ++index) {
      if (bits_[index] == 0U) { continue; }
      std::size_t const byte_index = index / 8U;
      unsigned const bit_index = static_cast<unsigned>(index % 8U);
      auto const mask = static_cast<unsigned char>(1U << (7U - bit_index));
      auto const current = static_cast<unsigned char>(packed.bytes_[byte_index]);
      packed.bytes_[byte_index] = static_cast<std::byte>(static_cast<unsigned char>(current | mask));
    }
    return packed;
  }

private:
  std::vector<std::uint8_t> bits_;
};

struct codeword
{
  std::uint32_t value_ = 0;
  unsigned width_ = 0;
};

[[nodiscard]] packed_bytes write_codewords(std::span<codeword const> words)
{
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  for (codeword const &word : words) { writer.write_bits(word.value_, word.width_); }
  packed_bytes packed;
  packed.padding_ = writer.finish();
  packed.bytes_ = std::move(bytes);
  return packed;
}

[[nodiscard]] packed_bytes reference_codewords(std::span<codeword const> words)
{
  reference_writer writer;
  for (codeword const &word : words) { writer.write_bits(word.value_, word.width_); }
  return writer.finish();
}

void check_codewords(std::span<codeword const> words, std::span<unsigned char const> expected, std::uint8_t padding)
{
  packed_bytes const actual = write_codewords(words);
  packed_bytes const reference = reference_codewords(words);
  std::vector<std::byte> expected_bytes;
  expected_bytes.reserve(expected.size());
  for (unsigned char byte : expected) { expected_bytes.push_back(static_cast<std::byte>(byte)); }
  REQUIRE(actual.padding_ == padding);
  REQUIRE(actual.bytes_ == expected_bytes);
  REQUIRE(reference.padding_ == padding);
  REQUIRE(reference.bytes_ == expected_bytes);
  REQUIRE(actual.padding_ <= 7);
}

class split_mix
{
public:
  explicit split_mix(std::uint32_t seed) : state_(seed) {}

  [[nodiscard]] std::uint32_t next()
  {
    state_ += 0x9E3779B9U;
    std::uint32_t value = state_;
    value = (value ^ (value >> 16U)) * 0x85EBCA6BU;
    value = (value ^ (value >> 13U)) * 0xC2B2AE35U;
    return value ^ (value >> 16U);
  }

private:
  std::uint32_t state_;
};

[[nodiscard]] std::vector<std::byte> random_bytes(std::uint32_t seed, std::size_t count)
{
  split_mix rng{ seed };
  std::vector<std::byte> bytes(count);
  for (std::size_t index = 0; index < count; ++index) {
    bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(rng.next()));
  }
  if (count > 0U) { bytes[0] = std::byte{ 0x00 }; }
  if (count > 1U) { bytes[1] = std::byte{ 0x80 }; }
  if (count > 2U) { bytes[2] = std::byte{ 0xFF }; }
  if (count > 3U) { bytes[3] = std::byte{ 0x7F }; }
  return bytes;
}

[[nodiscard]] std::vector<unsigned> chunk_widths(std::size_t bit_count, std::span<unsigned const> pattern)
{
  std::vector<unsigned> widths;
  std::size_t offset = 0;
  std::size_t pattern_index = 0;
  while (offset < bit_count) {
    unsigned width = pattern[pattern_index % pattern.size()];
    unsigned const room = static_cast<unsigned>(bit_count - offset);
    if (width > room) { width = room; }
    if (width > 32U) { width = 32U; }
    widths.push_back(width);
    offset += width;
    ++pattern_index;
  }
  return widths;
}

[[nodiscard]] packed_bytes write_bit_pattern(std::span<int const> bits, std::span<unsigned const> widths)
{
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  std::size_t offset = 0;
  for (unsigned width : widths) {
    std::uint32_t value = 0;
    for (unsigned bit = 0; bit < width; ++bit) {
      value = (value << 1U) | static_cast<std::uint32_t>(bits[offset + bit]);
    }
    offset += width;
    writer.write_bits(value, width);
  }
  packed_bytes packed;
  packed.padding_ = writer.finish();
  packed.bytes_ = std::move(bytes);
  return packed;
}

class fail_sink_iterator
{
public:
  struct state
  {
    std::vector<std::byte> bytes_;
    std::size_t assignments_ = 0;
    std::size_t increments_ = 0;
    std::size_t fail_at_ = 0;
    std::size_t fail_increment_at_ = 0;
  };

  using iterator_category = std::output_iterator_tag;
  using iterator_concept = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  explicit fail_sink_iterator(state *state) noexcept : state_(state) {}

  fail_sink_iterator &operator*() noexcept { return *this; }
  fail_sink_iterator const &operator*() const noexcept { return *this; }

  fail_sink_iterator const &operator=(std::byte value) const
  {
    ++state_->assignments_;
    if (state_->fail_at_ != 0U && state_->assignments_ >= state_->fail_at_) {
      throw std::ios_base::failure("injected output failure");
    }
    state_->bytes_.push_back(value);
    return *this;
  }

  fail_sink_iterator &operator++()
  {
    ++state_->increments_;
    if (state_->fail_increment_at_ != 0U && state_->increments_ >= state_->fail_increment_at_) {
      throw std::ios_base::failure("injected increment failure");
    }
    return *this;
  }

  fail_sink_iterator operator++(int)
  {
    fail_sink_iterator previous{ *this };
    ++*this;
    return previous;
  }

private:
  state *state_ = nullptr;
};

static_assert(std::output_iterator<fail_sink_iterator, std::byte>);

// Output iterator whose move can retarget the sink and then throw.
class retarget_iterator
{
public:
  struct control
  {
    std::vector<std::byte> *sink_ = nullptr;
    bool throw_on_move_construct_ = false;
    bool throw_on_move_assign_ = false;
  };

  using iterator_category = std::output_iterator_tag;
  using iterator_concept = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  explicit retarget_iterator(std::shared_ptr<control> control) : control_(std::move(control)) {}

  retarget_iterator(retarget_iterator const &) = default;
  retarget_iterator &operator=(retarget_iterator const &) = default;

  retarget_iterator(retarget_iterator &&other) : control_(std::move(other.control_))
  {
    if (control_ != nullptr && control_->throw_on_move_construct_) {
      throw std::runtime_error("move construction failed");
    }
  }

  retarget_iterator &operator=(retarget_iterator &&other)
  {
    if (control_ != nullptr && other.control_ != nullptr) { control_->sink_ = other.control_->sink_; }
    if (other.control_ != nullptr && other.control_->throw_on_move_assign_) {
      throw std::runtime_error("move assignment failed");
    }
    control_ = std::move(other.control_);
    return *this;
  }

  ~retarget_iterator() = default;

  retarget_iterator &operator*() noexcept { return *this; }
  retarget_iterator const &operator*() const noexcept { return *this; }

  retarget_iterator const &operator=(std::byte value) const
  {
    if (control_ == nullptr || control_->sink_ == nullptr) { throw std::runtime_error("moved-from output iterator"); }
    control_->sink_->push_back(value);
    return *this;
  }

  retarget_iterator &operator++() noexcept { return *this; }
  retarget_iterator operator++(int) noexcept { return *this; }

private:
  std::shared_ptr<control> control_;
};

static_assert(std::output_iterator<retarget_iterator, std::byte>);

template<class Action> void require_what(Action &&action, std::string_view expected)
{
  try {
    std::forward<Action>(action)();
    FAIL("expected an exception");
  } catch (std::exception const &error) {
    REQUIRE(std::string_view{ error.what() } == expected);
  }
}

}// namespace

struct bit_byte_proxy
{
  std::byte value_{};

  constexpr operator std::byte() const { return value_; }
};

template<template<class> class QualByte, template<class> class QualProxy>
struct std::basic_common_reference<std::byte, bit_byte_proxy, QualByte, QualProxy>
{
  using type = std::byte;
};

template<template<class> class QualProxy, template<class> class QualByte>
struct std::basic_common_reference<bit_byte_proxy, std::byte, QualProxy, QualByte>
{
  using type = std::byte;
};

class proxy_byte_view : public std::ranges::view_interface<proxy_byte_view>
{
public:
  explicit proxy_byte_view(std::vector<std::byte> data) : data_(std::move(data)) {}

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    [[nodiscard]] bit_byte_proxy operator*() const { return bit_byte_proxy{ *pointer_ }; }

    iterator &operator++()
    {
      ++pointer_;
      return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]] friend bool operator==(iterator const &it, std::default_sentinel_t) noexcept
    { return it.pointer_ == it.last_; }

  private:
    friend class proxy_byte_view;

    iterator(std::byte const *pointer, std::byte const *last) : pointer_(pointer), last_(last) {}

    std::byte const *pointer_ = nullptr;
    std::byte const *last_ = nullptr;
  };

  [[nodiscard]] auto begin() -> iterator { return iterator{ data_.data(), data_.data() + data_.size() }; }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
};

static_assert(std::input_iterator<proxy_byte_view::iterator>);
static_assert(dacoexec::byte_input_range<proxy_byte_view>);

TEST_CASE("bits yields the MSB-first pattern of each byte", "[bits]")
{
  REQUIRE(collect_bits(std::vector<std::byte>{} | dacoexec::views::bits()).empty());

  std::vector<std::byte> const zero{ std::byte{ 0x00 } };
  REQUIRE(collect_bits(zero | dacoexec::views::bits()) == std::vector<int>(8, 0));

  std::vector<std::byte> const ones{ std::byte{ 0xFF } };
  REQUIRE(collect_bits(ones | dacoexec::views::bits()) == std::vector<int>(8, 1));

  std::vector<std::byte> const high{ std::byte{ 0x80 } };
  REQUIRE(collect_bits(high | dacoexec::views::bits()) == std::vector<int>{ 1, 0, 0, 0, 0, 0, 0, 0 });

  std::vector<std::byte> const low{ std::byte{ 0x01 } };
  REQUIRE(collect_bits(low | dacoexec::views::bits()) == std::vector<int>{ 0, 0, 0, 0, 0, 0, 0, 1 });

  std::vector<std::byte> const bytes{ std::byte{ 0xB2 }, std::byte{ 0x80 } };
  std::vector<int> const expected{ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0 };
  REQUIRE(collect_bits(bytes | dacoexec::views::bits()) == expected);
  REQUIRE(collect_bits(dacoexec::views::bits()(bytes)) == expected);
  REQUIRE(reference_bits(bytes) == expected);
}

TEST_CASE("bits preserves high bits of every byte character type", "[bits]")
{
  auto check = [](auto value) {
    using value_type = decltype(value);
    std::vector<value_type> input;
    std::array<unsigned char, 5> const patterns{ 0xB2, 0x80, 0x01, 0x00, 0xFF };
    for (unsigned char pattern : patterns) { input.push_back(std::bit_cast<value_type>(pattern)); }
    auto const bits = collect_bits(input | dacoexec::views::bits());
    REQUIRE(std::vector<int>(bits.begin(), bits.begin() + 8) == std::vector<int>{ 1, 0, 1, 1, 0, 0, 1, 0 });
    REQUIRE(std::vector<int>(bits.begin() + 8, bits.begin() + 16) == std::vector<int>{ 1, 0, 0, 0, 0, 0, 0, 0 });
    REQUIRE(std::vector<int>(bits.begin() + 16, bits.begin() + 24) == std::vector<int>{ 0, 0, 0, 0, 0, 0, 0, 1 });
    REQUIRE(std::vector<int>(bits.begin() + 24, bits.begin() + 32) == std::vector<int>(8, 0));
    REQUIRE(std::vector<int>(bits.begin() + 32, bits.end()) == std::vector<int>(8, 1));
  };

  check(char{});
  check(static_cast<signed char>(0));
  check(static_cast<unsigned char>(0));
  check(char8_t{});
}

TEST_CASE("bits reads a proxy-backed byte range once per byte", "[bits]")
{
  std::vector<std::byte> const input{ std::byte{ 0xB2 }, std::byte{ 0x0F } };
  REQUIRE(collect_bits(proxy_byte_view{ input } | dacoexec::views::bits()) == reference_bits(input));
}

TEST_CASE("bits accepts spans, arrays, and owning temporaries", "[bits]")
{
  std::vector<std::byte> const expected = make_bytes(5);
  REQUIRE(collect_bits(expected | dacoexec::views::bits()) == reference_bits(expected));

  std::array<std::byte, 5> array{};
  std::ranges::copy(expected, array.begin());
  REQUIRE(collect_bits(array | dacoexec::views::bits()) == reference_bits(expected));

  std::span<std::byte const> span{ expected };
  REQUIRE(collect_bits(span | dacoexec::views::bits()) == reference_bits(expected));
  REQUIRE(collect_bits(make_bytes(5) | dacoexec::views::bits()) == reference_bits(expected));
}

TEST_CASE("constructing a bit view reads nothing and caches one byte", "[bits]")
{
  auto stats = std::make_shared<source_stats>();
  byte_source source{ make_bytes(4), stats };
  auto view = std::move(source) | dacoexec::views::bits();
  REQUIRE(stats->begin_calls_ == 0);
  REQUIRE(stats->dereferences_ == 0);
  REQUIRE(stats->increments_ == 0);

  auto it = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == 1);
  REQUIRE(stats->increments_ == 0);
  bool const first = *it;
  REQUIRE(*it == first);
  REQUIRE(stats->dereferences_ == 1);

  for (unsigned step = 0; step < 7U; ++step) { ++it; }
  REQUIRE(stats->dereferences_ == 1);
  REQUIRE(stats->increments_ == 0);

  ++it;
  REQUIRE(stats->dereferences_ == 2);
  REQUIRE(stats->increments_ == 1);

  auto again = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(*again == *it);
}

TEST_CASE("copied bit iterators share one single-pass position", "[bits]")
{
  std::vector<std::byte> const input{ std::byte{ 0xB2 } };
  auto view = input | dacoexec::views::bits();
  auto it = view.begin();
  auto copy = it;
  REQUIRE(*it);
  REQUIRE(*copy);
  ++copy;
  REQUIRE(!*it);
  REQUIRE(!*copy);
}

TEST_CASE("moving a started bit view keeps the cached position", "[bits]")
{
  std::vector<std::byte> const input{ std::byte{ 0xB2 }, std::byte{ 0x80 } };
  auto view = input | dacoexec::views::bits();
  auto it = view.begin();
  REQUIRE(*it);
  auto moved = std::move(view);

  std::vector<int> collected;
  collected.push_back(*it ? 1 : 0);
  ++it;
  while (it != moved.end()) {
    collected.push_back(*it ? 1 : 0);
    ++it;
  }
  REQUIRE(collected == reference_bits(input));

  std::size_t extra = 0;
  for (bool bit : view) {
    ++extra;
    (void)bit;
  }
  REQUIRE(extra == 0);
}

TEST_CASE("a move-only bit source is read once", "[bits]")
{
  auto stats = std::make_shared<source_stats>();
  constexpr std::size_t count = 10;
  std::vector<std::byte> const input = make_bytes(count);
  byte_source source{ input, stats };
  auto const collected = collect_bits(std::move(source) | dacoexec::views::bits());
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == count);
  REQUIRE(stats->increments_ == count);
  REQUIRE(collected == reference_bits(input));
  REQUIRE(collected.size() == count * 8U);
}

TEST_CASE("an empty move-only source produces no bits", "[bits]")
{
  auto stats = std::make_shared<source_stats>();
  byte_source source{ {}, stats };
  auto view = std::move(source) | dacoexec::views::bits();
  REQUIRE(stats->begin_calls_ == 0);
  REQUIRE(view.begin() == std::default_sentinel);
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == 0);
  REQUIRE(stats->increments_ == 0);
}

TEST_CASE("upstream bit-source failure propagates and stays failed", "[bits]")
{
  std::vector<std::byte> const input{ std::byte{ 0xB2 }, std::byte{ 0x80 } };

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = 1;
    byte_source source{ input, stats };
    auto view = std::move(source) | dacoexec::views::bits();
    std::vector<int> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (bool bit : view) { kept.push_back(bit ? 1 : 0); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept.empty());
    std::size_t extra = 0;
    for (bool bit : view) {
      ++extra;
      (void)bit;
    }
    REQUIRE(extra == 0);
  }

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = 2;
    byte_source source{ input, stats };
    auto view = std::move(source) | dacoexec::views::bits();
    std::vector<int> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (bool bit : view) { kept.push_back(bit ? 1 : 0); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept == std::vector<int>{ 1, 0, 1, 1, 0, 0, 1, 0 });
    std::size_t extra = 0;
    for (bool bit : view) {
      ++extra;
      (void)bit;
    }
    REQUIRE(extra == 0);
  }

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_increment_ = 1;
    byte_source source{ input, stats };
    std::vector<int> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (bool bit : std::move(source) | dacoexec::views::bits()) { kept.push_back(bit ? 1 : 0); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept == std::vector<int>{ 1, 0, 1, 1, 0, 0, 1, 0 });
  }
}

TEST_CASE("a throwing source begin leaves the bit view unusable", "[bits]")
{
  auto stats = std::make_shared<source_stats>();
  stats->fail_begin_ = true;
  byte_source source{ { std::byte{ 0xB2 } }, stats };
  auto view = std::move(source) | dacoexec::views::bits();
  REQUIRE(stats->begin_calls_ == 0);
  REQUIRE_THROWS_AS(view.begin(), std::ios_base::failure);
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == 0);

  std::size_t extra = 0;
  for (bool bit : view) {
    ++extra;
    (void)bit;
  }
  REQUIRE(extra == 0);
  REQUIRE(stats->begin_calls_ == 1);
}

TEST_CASE("a streamed bit view reads one byte at a time", "[bits]")
{
  constexpr std::size_t small = 32;
  auto stats = std::make_shared<source_stats>();
  std::vector<std::byte> const input = make_bytes(small);
  byte_source source{ input, stats };
  auto view = std::move(source) | dacoexec::views::bits();
  std::size_t seen = 0;
  for (bool bit : view) {
    ++seen;
    REQUIRE(stats->dereferences_ == ((seen - 1U) / 8U) + 1U);
    REQUIRE(stats->increments_ == (seen - 1U) / 8U);
    (void)bit;
  }
  REQUIRE(seen == small * 8U);
  REQUIRE(stats->dereferences_ == small);
  REQUIRE(stats->increments_ == small);
  REQUIRE(stats->begin_calls_ == 1);

  constexpr std::size_t large = 4096;
  auto large_stats = std::make_shared<source_stats>();
  std::vector<std::byte> const large_input = make_bytes(large);
  byte_source large_source{ large_input, large_stats };
  std::size_t large_seen = 0;
  for (bool bit : std::move(large_source) | dacoexec::views::bits()) {
    std::size_t const byte_index = large_seen / 8U;
    unsigned const bit_index = static_cast<unsigned>(large_seen % 8U);
    unsigned const value = std::to_integer<unsigned>(large_input[byte_index]);
    bool const want = ((value >> (7U - bit_index)) & 1U) != 0U;
    REQUIRE(bit == want);
    ++large_seen;
  }
  REQUIRE(large_seen == large * 8U);
  REQUIRE(large_stats->dereferences_ == large);
  REQUIRE(large_stats->increments_ == large);
  REQUIRE(large_stats->begin_calls_ == 1);
}

TEST_CASE("buffered bytes cross window boundaries into bits and back", "[bits][buffered]")
{
  std::vector<std::byte> const input = make_bytes(8);
  auto stats = std::make_shared<source_stats>();
  byte_source source{ input, stats };
  auto view = std::move(source) | dacoexec::views::buffered_bytes(3);
  REQUIRE(stats->begin_calls_ == 0);

  std::vector<std::byte> packed;
  dacoexec::bit_writer writer{ std::back_inserter(packed) };
  std::vector<int> collected;
  std::size_t seen = 0;
  for (bool bit : std::move(view) | dacoexec::views::bits()) {
    if (seen == 0U) { REQUIRE(stats->dereferences_ == 3U); }
    if (seen == 24U) { REQUIRE(stats->dereferences_ == 6U); }
    collected.push_back(bit ? 1 : 0);
    writer.write_bit(bit);
    ++seen;
  }

  REQUIRE(seen == input.size() * 8U);
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == input.size());
  REQUIRE(stats->increments_ == input.size());
  REQUIRE(collected == reference_bits(input));
  REQUIRE(writer.finish() == 0);
  REQUIRE(packed == input);

  auto one_stats = std::make_shared<source_stats>();
  byte_source one_source{ input, one_stats };
  std::vector<std::byte> one_packed;
  dacoexec::bit_writer one_writer{ std::back_inserter(one_packed) };
  std::size_t one_seen = 0;
  for (bool bit : std::move(one_source) | dacoexec::views::buffered_bytes(1) | dacoexec::views::bits()) {
    if (one_seen == 0U) { REQUIRE(one_stats->dereferences_ == 1U); }
    if (one_seen == 8U) { REQUIRE(one_stats->dereferences_ == 2U); }
    one_writer.write_bit(bit);
    ++one_seen;
  }
  REQUIRE(one_writer.finish() == 0);
  REQUIRE(one_packed == input);
  REQUIRE(one_stats->dereferences_ == input.size());
}

TEST_CASE("buffered bytes yield the source across partial windows", "[buffered]")
{
  std::vector<std::byte> const input = make_bytes(8);
  std::vector<std::byte> collected;
  for (std::byte value : input | dacoexec::views::buffered_bytes(3)) { collected.push_back(value); }
  REQUIRE(collected == input);

  collected.clear();
  for (std::byte value : input | dacoexec::views::buffered_bytes(8)) { collected.push_back(value); }
  REQUIRE(collected == input);

  collected.clear();
  for (std::byte value : input | dacoexec::views::buffered_bytes(64)) { collected.push_back(value); }
  REQUIRE(collected == input);

  std::vector<std::byte> empty;
  auto empty_view = empty | dacoexec::views::buffered_bytes(4);
  REQUIRE(empty_view.begin() == std::default_sentinel);
  auto huge = empty | dacoexec::views::buffered_bytes(std::numeric_limits<std::size_t>::max());
  REQUIRE(huge.begin() == std::default_sentinel);
  std::vector<std::byte> const one{ std::byte{ 0xB2 } };
  collected.clear();
  for (std::byte value : one | dacoexec::views::buffered_bytes(std::numeric_limits<std::size_t>::max())) {
    collected.push_back(value);
  }
  REQUIRE(collected == one);
  REQUIRE_THROWS_AS(dacoexec::views::buffered_bytes(0), std::invalid_argument);

  std::vector<unsigned char> const chars{ 0x00, 0x80, 0xFF, 0xB2 };
  collected.clear();
  for (std::byte value : chars | dacoexec::views::buffered_bytes(2)) { collected.push_back(value); }
  REQUIRE(
    collected == std::vector<std::byte>{ std::byte{ 0x00 }, std::byte{ 0x80 }, std::byte{ 0xFF }, std::byte{ 0xB2 } });
}

TEST_CASE("a buffered byte window stays lazy and resumes after a move", "[buffered]")
{
  std::vector<std::byte> const input = make_bytes(5);
  auto stats = std::make_shared<source_stats>();
  byte_source source{ input, stats };
  auto view = std::move(source) | dacoexec::views::buffered_bytes(2);
  REQUIRE(stats->begin_calls_ == 0);

  auto cursor = view.begin();
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == 2);
  REQUIRE(*cursor == input[0]);
  ++cursor;
  REQUIRE(*cursor == input[1]);
  REQUIRE(stats->dereferences_ == 2);

  auto moved = std::move(view);
  ++cursor;
  REQUIRE(stats->dereferences_ == 4);
  REQUIRE(*cursor == input[2]);

  std::vector<std::byte> rest;
  while (cursor != std::default_sentinel) {
    rest.push_back(*cursor);
    ++cursor;
  }
  REQUIRE(rest == std::vector<std::byte>(input.begin() + 2, input.end()));
  REQUIRE(stats->dereferences_ == input.size());

  std::size_t extra = 0;
  for (std::byte value : view) {
    ++extra;
    (void)value;
  }
  REQUIRE(extra == 0);
}

TEST_CASE("a buffered read failure discards the unfinished window", "[buffered]")
{
  std::vector<std::byte> const input = make_bytes(8);

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = 2;
    byte_source source{ input, stats };
    auto view = std::move(source) | dacoexec::views::buffered_bytes(3);
    REQUIRE_THROWS_AS(view.begin(), std::ios_base::failure);
    std::size_t extra = 0;
    for (std::byte value : view) {
      ++extra;
      (void)value;
    }
    REQUIRE(extra == 0);
    REQUIRE(stats->begin_calls_ == 1);
  }

  {
    auto stats = std::make_shared<source_stats>();
    stats->fail_at_dereference_ = 4;
    byte_source source{ input, stats };
    auto view = std::move(source) | dacoexec::views::buffered_bytes(3);
    std::vector<std::byte> kept;
    REQUIRE_THROWS_AS(
      [&] {
        for (std::byte value : view) { kept.push_back(value); }
      }(),
      std::ios_base::failure);
    REQUIRE(kept == std::vector<std::byte>(input.begin(), input.begin() + 3));
    std::size_t extra = 0;
    for (std::byte value : view) {
      ++extra;
      (void)value;
    }
    REQUIRE(extra == 0);
  }
}

TEST_CASE("bit views of blocks reconstruct the original bytes", "[bits]")
{
  std::vector<std::byte> const input = make_bytes(50);
  std::vector<int> through_blocks;
  std::vector<dacoexec::byte_block> owned;
  for (auto block : input | dacoexec::views::blocks(16)) { owned.push_back(std::move(block)); }
  for (dacoexec::byte_block &block : owned) {
    auto part = collect_bits(block.bytes_ | dacoexec::views::bits());
    through_blocks.insert(through_blocks.end(), part.begin(), part.end());
  }
  REQUIRE(through_blocks == reference_bits(input));

  std::vector<std::byte> packed;
  dacoexec::bit_writer writer{ std::back_inserter(packed) };
  for (dacoexec::byte_block const &block : owned) {
    std::ranges::copy(block.bytes_ | dacoexec::views::bits(), writer.output_iterator());
  }
  REQUIRE(writer.finish() == 0);
  REQUIRE(packed == input);
}

TEST_CASE("bit_writer golden vectors", "[bit_writer]")
{
  check_codewords({}, {}, 0);
  check_codewords(std::array{ codeword{ 0U, 0U } }, {}, 0);
  check_codewords(std::array{ codeword{ 1U, 1U } }, std::array<unsigned char, 1>{ 0x80 }, 7);
  check_codewords(std::array{ codeword{ 0b101U, 3U } }, std::array<unsigned char, 1>{ 0xA0 }, 5);
  check_codewords(std::array{ codeword{ 0b10110U, 5U } }, std::array<unsigned char, 1>{ 0xB0 }, 3);
  check_codewords(std::array{ codeword{ 0b10110010U, 8U } }, std::array<unsigned char, 1>{ 0xB2 }, 0);
  check_codewords(
    std::array{ codeword{ 0b101U, 3U }, codeword{ 0b11001U, 5U } }, std::array<unsigned char, 1>{ 0xB9 }, 0);
  check_codewords(
    std::array{ codeword{ 0b1011U, 4U }, codeword{ 0b01011U, 5U } }, std::array<unsigned char, 2>{ 0xB5, 0x80 }, 7);
  check_codewords(
    std::array{ codeword{ 0xFFFFFFFFU, 32U } }, std::array<unsigned char, 4>{ 0xFF, 0xFF, 0xFF, 0xFF }, 0);
  check_codewords(
    std::array{ codeword{ 0x80000000U, 32U } }, std::array<unsigned char, 4>{ 0x80, 0x00, 0x00, 0x00 }, 0);
  check_codewords(std::array{ codeword{ 0x1U, 32U } }, std::array<unsigned char, 4>{ 0x00, 0x00, 0x00, 0x01 }, 0);
  check_codewords(std::array{ codeword{ 0U, 32U } }, std::array<unsigned char, 4>{ 0x00, 0x00, 0x00, 0x00 }, 0);
  check_codewords(std::array{ codeword{ 0x3FFU, 10U } }, std::array<unsigned char, 2>{ 0xFF, 0xC0 }, 6);
  check_codewords(std::array{ codeword{ 1U, 2U } }, std::array<unsigned char, 1>{ 0x40 }, 6);
  check_codewords(std::array{ codeword{ 0b111U, 3U },
                    codeword{ 0b000U, 3U },
                    codeword{ 0b111U, 3U },
                    codeword{ 0b000U, 3U },
                    codeword{ 0b101U, 3U } },
    std::array<unsigned char, 2>{ 0xE3, 0x8A },
    1);
}

TEST_CASE("write_bit matches the codeword order", "[bit_writer]")
{
  std::vector<std::byte> out;
  dacoexec::bit_writer writer{ std::back_inserter(out) };
  writer.write_bit(true);
  REQUIRE(writer.finish() == 7);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0x80 } });

  out.clear();
  dacoexec::bit_writer zeros{ std::back_inserter(out) };
  zeros.write_bit(false);
  REQUIRE(zeros.finish() == 7);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0x00 } });

  out.clear();
  dacoexec::bit_writer pattern{ std::back_inserter(out) };
  for (int bit : std::vector<int>{ 1, 0, 1, 1, 0, 0, 1, 0 }) { pattern.write_bit(bit != 0); }
  REQUIRE(pattern.finish() == 0);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xB2 } });

  out.clear();
  dacoexec::bit_writer ones{ std::back_inserter(out) };
  for (int count = 0; count < 8; ++count) { ones.write_bit(true); }
  REQUIRE(ones.finish() == 0);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xFF } });
}

TEST_CASE("a zero-width write does not change alignment", "[bit_writer]")
{
  std::vector<codeword> const words{ { 0b101U, 3U }, { 0U, 0U }, { 0b11001U, 5U } };
  check_codewords(words, std::array<unsigned char, 1>{ 0xB9 }, 0);
}

TEST_CASE("invalid codewords are rejected without writing", "[bit_writer]")
{
  std::vector<std::byte> out;
  dacoexec::bit_writer writer{ std::back_inserter(out) };
  writer.write_bit(true);
  REQUIRE_THROWS_AS(writer.write_bits(1U, 0U), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.write_bits(0U, 33U), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.write_bits(0U, 100U), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.write_bits(0x1FFU, 8U), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.write_bits(2U, 1U), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.write_bits(0xFFFFFFFFU, 31U), std::invalid_argument);
  REQUIRE(writer.finish() == 7);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0x80 } });
  require_what([&] { writer.write_bits(1U, 0U); }, "dacoexec::bit_writer write after finalization");
  require_what([&] { writer.write_bits(0U, 0U); }, "dacoexec::bit_writer write after finalization");
  REQUIRE(out.size() == 1);
}

TEST_CASE("finish pads with zeros and is idempotent", "[bit_writer]")
{
  std::vector<std::byte> out;
  {
    dacoexec::bit_writer writer{ std::back_inserter(out) };
    REQUIRE(writer.finish() == 0);
    REQUIRE(writer.finish() == 0);
    REQUIRE(out.empty());
  }

  out.clear();
  dacoexec::bit_writer writer{ std::back_inserter(out) };
  writer.write_bits(0b10110U, 5U);
  REQUIRE(writer.finish() == 3);
  REQUIRE(writer.finish() == 3);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xB0 } });
  REQUIRE_THROWS_AS(writer.write_bit(true), std::logic_error);
  REQUIRE_THROWS_AS(writer.write_bits(0U, 0U), std::logic_error);
  REQUIRE_THROWS_AS(writer.write_bits(0U, 8U), std::logic_error);
  REQUIRE(out.size() == 1);
}

TEST_CASE("the writer destructor does not flush a partial byte", "[bit_writer]")
{
  std::vector<std::byte> out;
  {
    dacoexec::bit_writer writer{ std::back_inserter(out) };
    writer.write_bits(0b101U, 3U);
  }
  REQUIRE(out.empty());
}

TEST_CASE("moving a writer transfers the partial byte", "[bit_writer]")
{
  std::vector<std::byte> out;
  dacoexec::bit_writer writer{ std::back_inserter(out) };
  writer.write_bits(0b101U, 3U);
  auto moved = std::move(writer);
  REQUIRE_THROWS_AS(writer.write_bit(true), std::logic_error);
  REQUIRE_THROWS_AS(writer.finish(), std::logic_error);
  REQUIRE(moved.finish() == 5);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xA0 } });
}

TEST_CASE("move assignment discards the destination partial byte", "[bit_writer]")
{
  std::vector<std::byte> sink_a;
  std::vector<std::byte> sink_b;
  dacoexec::bit_writer destination{ std::back_inserter(sink_a) };
  destination.write_bits(0b111U, 3U);
  dacoexec::bit_writer source{ std::back_inserter(sink_b) };
  source.write_bits(0b101U, 3U);

  destination = std::move(source);
  REQUIRE_THROWS_AS(source.finish(), std::logic_error);
  REQUIRE(destination.finish() == 5);
  REQUIRE(sink_a.empty());
  REQUIRE(sink_b == std::vector<std::byte>{ std::byte{ 0xA0 } });
}

TEST_CASE("a throwing output move leaves both writers unusable", "[bit_writer]")
{
  std::vector<std::byte> sink_a;
  std::vector<std::byte> sink_b;

  {
    auto control = std::make_shared<retarget_iterator::control>();
    control->sink_ = &sink_a;
    dacoexec::bit_writer writer{ retarget_iterator{ control } };
    writer.write_bits(0b101U, 3U);
    control->throw_on_move_construct_ = true;
    require_what(
      [&] {
        dacoexec::bit_writer moved{ std::move(writer) };
        (void)moved;
      },
      "move construction failed");
    require_what([&] { (void)writer.finish(); }, "dacoexec::bit_writer is unusable");
    REQUIRE(sink_a.empty());
  }

  {
    auto control_a = std::make_shared<retarget_iterator::control>();
    auto control_b = std::make_shared<retarget_iterator::control>();
    control_a->sink_ = &sink_a;
    control_b->sink_ = &sink_b;
    control_b->throw_on_move_assign_ = true;
    dacoexec::bit_writer destination{ retarget_iterator{ control_a } };
    destination.write_bits(0b101U, 3U);
    dacoexec::bit_writer source{ retarget_iterator{ control_b } };
    require_what([&] { destination = std::move(source); }, "move assignment failed");
    require_what([&] { (void)destination.finish(); }, "dacoexec::bit_writer is unusable");
    require_what([&] { (void)source.finish(); }, "dacoexec::bit_writer is unusable");
    REQUIRE(sink_a.empty());
    REQUIRE(sink_b.empty());
  }
}

TEST_CASE("an output failure stops the writer without rolling back", "[bit_writer]")
{
  {
    fail_sink_iterator::state state;
    state.fail_at_ = 1;
    dacoexec::bit_writer writer{ fail_sink_iterator{ &state } };
    REQUIRE_THROWS_AS(writer.write_bits(0xFFFFU, 16U), std::ios_base::failure);
    REQUIRE(state.bytes_.empty());
    REQUIRE_THROWS_AS(writer.write_bit(true), std::logic_error);
    REQUIRE_THROWS_AS(writer.finish(), std::logic_error);
    REQUIRE(state.bytes_.empty());
  }

  {
    fail_sink_iterator::state state;
    state.fail_at_ = 2;
    dacoexec::bit_writer writer{ fail_sink_iterator{ &state } };
    REQUIRE_THROWS_AS(writer.write_bits(0xFFFFFFFFU, 32U), std::ios_base::failure);
    REQUIRE(state.bytes_.size() == 1);
    REQUIRE(state.bytes_[0] == std::byte{ 0xFF });
    REQUIRE_THROWS_AS(writer.write_bit(false), std::logic_error);
    REQUIRE_THROWS_AS(writer.finish(), std::logic_error);
    REQUIRE(state.bytes_.size() == 1);
  }

  {
    fail_sink_iterator::state state;
    state.fail_at_ = 1;
    dacoexec::bit_writer writer{ fail_sink_iterator{ &state } };
    writer.write_bits(0b1011U, 4U);
    REQUIRE(state.bytes_.empty());
    REQUIRE_THROWS_AS(writer.finish(), std::ios_base::failure);
    REQUIRE(state.bytes_.empty());
    REQUIRE_THROWS_AS(writer.finish(), std::logic_error);
  }

  {
    fail_sink_iterator::state state;
    state.fail_increment_at_ = 1;
    dacoexec::bit_writer writer{ fail_sink_iterator{ &state } };
    REQUIRE_THROWS_AS(writer.write_bits(0xB280U, 16U), std::ios_base::failure);
    REQUIRE(state.assignments_ == 1);
    REQUIRE(state.bytes_ == std::vector<std::byte>{ std::byte{ 0xB2 } });
    REQUIRE_THROWS_AS(writer.write_bit(true), std::logic_error);
    REQUIRE_THROWS_AS(writer.finish(), std::logic_error);
    REQUIRE(state.bytes_.size() == 1);
  }
}

TEST_CASE("bit output iterators share the writer and feed ranges::copy", "[bit_writer]")
{
  std::vector<std::byte> out;
  dacoexec::bit_writer writer{ std::back_inserter(out) };
  auto first = writer.output_iterator();
  auto second = first;
  *first = true;
  ++first;
  ++first;
  *second = true;
  REQUIRE(writer.finish() == 6);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xC0 } });

  out.clear();
  dacoexec::bit_writer copied{ std::back_inserter(out) };
  std::array<bool, 4> const flags{ true, false, true, true };
  std::ranges::copy(flags, copied.output_iterator());
  REQUIRE(copied.finish() == 4);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xB0 } });

  out.clear();
  dacoexec::bit_writer classic{ std::back_inserter(out) };
  std::copy(flags.begin(), flags.end(), classic.output_iterator());
  REQUIRE(classic.finish() == 4);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xB0 } });

  out.clear();
  dacoexec::bit_writer stepped{ std::back_inserter(out) };
  auto cursor = stepped.output_iterator();
  *cursor++ = true;
  *cursor++ = false;
  *cursor++ = true;
  REQUIRE(stepped.finish() == 5);
  REQUIRE(out == std::vector<std::byte>{ std::byte{ 0xA0 } });
}

TEST_CASE("writing into an existing byte buffer packs in place", "[bit_writer]")
{
  std::vector<std::byte> buffer(4);
  dacoexec::bit_writer writer{ buffer.begin() };
  writer.write_bits(0xFFFFFFFFU, 32U);
  REQUIRE(writer.finish() == 0);
  REQUIRE(buffer == std::vector<std::byte>(4, std::byte{ 0xFF }));
}

TEST_CASE("pack of unpacked bytes is the original byte sequence", "[bit_writer]")
{
  auto check = [](std::vector<std::byte> const &input) {
    REQUIRE(collect_bits(input | dacoexec::views::bits()).size() == input.size() * 8U);
    std::vector<std::byte> packed;
    dacoexec::bit_writer writer{ std::back_inserter(packed) };
    std::ranges::copy(input | dacoexec::views::bits(), writer.output_iterator());
    REQUIRE(writer.finish() == 0);
    REQUIRE(packed == input);
  };

  check({});
  for (std::size_t count = 0; count <= 64U; ++count) { check(make_bytes(count)); }
  check(random_bytes(0xC0FFEEU, 127));
  check(random_bytes(1U, 128));
  check(random_bytes(42U, 255));
  check(random_bytes(99U, 256));
  check(random_bytes(7U, 1024));
  check(random_bytes(11U, 4096));
}

TEST_CASE("random codewords match an independent bit packer", "[bit_writer]")
{
  std::array<std::uint32_t, 3> const seeds{ 0xC0FFEEU, 1U, 42U };
  for (std::uint32_t seed : seeds) {
    split_mix rng{ seed };
    std::vector<codeword> words;
    words.reserve(300);
    for (int count = 0; count < 300; ++count) {
      unsigned const width = rng.next() % 33U;
      std::uint32_t value = 0;
      if (width == 32U) {
        value = rng.next();
      } else if (width > 0U) {
        value = rng.next() & dacoexec::detail::low_bit_mask(width);
      }
      words.push_back(codeword{ value, width });
    }
    packed_bytes const actual = write_codewords(words);
    packed_bytes const reference = reference_codewords(words);
    REQUIRE(actual.bytes_ == reference.bytes_);
    REQUIRE(actual.padding_ == reference.padding_);
    REQUIRE(actual.padding_ <= 7);
  }
}

TEST_CASE("different splits of one bit sequence serialize the same bytes", "[bit_writer]")
{
  std::vector<std::byte> const input{
    std::byte{ 0x00 }, std::byte{ 0xFF }, std::byte{ 0x80 }, std::byte{ 0x01 }, std::byte{ 0xB2 }, std::byte{ 0x7F }
  };
  std::vector<int> const bits = reference_bits(input);
  std::array<unsigned, 1> const singles{ 1U };
  std::array<unsigned, 1> const bytes{ 8U };
  std::array<unsigned, 5> const mixed{ 3U, 5U, 7U, 1U, 32U };
  std::array<unsigned, 2> const wide{ 32U, 16U };

  packed_bytes const by_bit = write_bit_pattern(bits, chunk_widths(bits.size(), singles));
  packed_bytes const by_byte = write_bit_pattern(bits, chunk_widths(bits.size(), bytes));
  packed_bytes const by_mixed = write_bit_pattern(bits, chunk_widths(bits.size(), mixed));
  packed_bytes const by_wide = write_bit_pattern(bits, chunk_widths(bits.size(), wide));

  REQUIRE(by_bit.padding_ == 0);
  REQUIRE(by_bit.bytes_ == input);
  REQUIRE(by_byte.bytes_ == input);
  REQUIRE(by_mixed.bytes_ == input);
  REQUIRE(by_wide.bytes_ == input);
  REQUIRE(by_byte.padding_ == by_bit.padding_);
  REQUIRE(by_mixed.padding_ == by_bit.padding_);
  REQUIRE(by_wide.padding_ == by_bit.padding_);

  std::vector<std::byte> out;
  dacoexec::bit_writer writer{ std::back_inserter(out) };
  for (std::byte byte : input) { writer.write_bits(std::to_integer<std::uint32_t>(byte), 8U); }
  REQUIRE(writer.finish() == 0);
  REQUIRE(out == input);
}
