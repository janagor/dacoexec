#include "huffman_test_utils.hpp"
#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/huffman/canonical.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/tree.hpp>

#include <dacoexec/views/blocks.hpp>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr auto splitmix_increment = 0x9E3779B97F4A7C15ULL;
constexpr auto splitmix_first_shift = 30U;
constexpr auto splitmix_first_multiplier = 0xBF58476D1CE4E5B9ULL;
constexpr auto splitmix_second_shift = 27U;
constexpr auto splitmix_second_multiplier = 0x94D049BB133111EBULL;
constexpr auto splitmix_final_shift = 31U;
constexpr auto limb_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr auto high_bit_value = 0x80;
constexpr auto all_bits_value = 0xFF;
constexpr auto sample_a_count = 5;
constexpr auto singleton_frequency = 100;
constexpr auto heavy_frequency = 1000;
constexpr auto frequency_scale = 7;
constexpr auto last_symbol = dacoexec::huffman::alphabet_size - 1U;
constexpr auto high_frequency_shift = limb_bits - 1U;
constexpr auto quarter_frequency_shift = limb_bits - 2U;
constexpr auto histogram_multiplier = 17U;
constexpr auto histogram_modulus = 11U;
constexpr auto small_histogram_cases = 128;
constexpr auto wide_histogram_cases = 24;


constexpr std::uint64_t histogram_seed = 0xA11CE5EEDULL;

[[nodiscard]] constexpr auto symbol(char value) -> unsigned char { return static_cast<unsigned char>(value); }

template<class R>
concept can_count_frequencies = requires(R &&range) { dacoexec::huffman::count_frequencies(std::forward<R>(range)); };

static_assert(can_count_frequencies<std::vector<std::byte>>);
static_assert(can_count_frequencies<std::vector<char>>);
static_assert(can_count_frequencies<std::vector<unsigned char>>);
static_assert(can_count_frequencies<std::vector<signed char>>);
static_assert(can_count_frequencies<std::vector<char8_t>>);
static_assert(can_count_frequencies<std::string_view>);
static_assert(!can_count_frequencies<std::vector<int>>);
static_assert(!can_count_frequencies<std::vector<std::uint16_t>>);

struct source_stats
{
  std::size_t begin_calls_ = 0;
  std::size_t dereferences_ = 0;
  std::size_t increments_ = 0;
  std::optional<std::size_t> fail_at_dereference_;
};

class injected_read_error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// Move-only, unsized, single-pass byte source. begin() does not rewind.
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
        throw injected_read_error("injected read failure");
      }
      return parent_->data_.at(parent_->cursor_);
    }

    auto operator++() -> iterator &
    {
      ++parent_->stats_->increments_;
      ++parent_->cursor_;
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

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
    return iterator{ this };
  }

  [[nodiscard]] static auto end() noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
  std::shared_ptr<source_stats> stats_;
  std::size_t cursor_ = 0;
};

static_assert(std::ranges::input_range<byte_source>);
static_assert(!std::ranges::forward_range<byte_source>);
static_assert(!std::ranges::sized_range<byte_source>);
static_assert(!std::copyable<byte_source>);
static_assert(std::movable<byte_source>);
static_assert(dacoexec::byte_input_range<byte_source>);

class splitmix64
{
public:
  explicit splitmix64(std::uint64_t seed) : state_(seed) {}

  [[nodiscard]] auto next() -> std::uint64_t
  {
    std::uint64_t mixed = (state_ += splitmix_increment);
    mixed = (mixed ^ (mixed >> splitmix_first_shift)) * splitmix_first_multiplier;
    mixed = (mixed ^ (mixed >> splitmix_second_shift)) * splitmix_second_multiplier;
    return mixed ^ (mixed >> splitmix_final_shift);
  }

private:
  std::uint64_t state_;
};

struct wide
{
  std::array<std::uint64_t, 4> limb_{};

  [[nodiscard]] friend auto operator==(wide const &left, wide const &right) -> bool = default;
};

[[nodiscard]] auto one_shifted(unsigned shift) -> wide
{
  wide result;
  result.limb_.at(static_cast<std::size_t>(shift / limb_bits)) = std::uint64_t{ 1 } << (shift % limb_bits);
  return result;
}

auto add_wide(wide &sum, wide const &value) -> void
{
  std::uint64_t carry = 0;
  for (std::size_t index = 0; index < sum.limb_.size(); ++index) {
    std::uint64_t const left = sum.limb_.at(index);
    std::uint64_t const right = value.limb_.at(index);
    std::uint64_t const with_carry = left + carry;
    std::uint64_t const carry_left = with_carry < left ? std::uint64_t{ 1 } : std::uint64_t{ 0 };
    std::uint64_t const total = with_carry + right;
    std::uint64_t const carry_right = total < with_carry ? std::uint64_t{ 1 } : std::uint64_t{ 0 };
    sum.limb_.at(index) = total;
    carry = carry_left + carry_right;
  }
  REQUIRE(carry == 0);
}

// Kraft equality in units of 2^(-max_length). A complete code sums to 2^max_length.
auto check_kraft(dacoexec::huffman::frequency_table const &frequencies,
  dacoexec::huffman::code_length_table const &lengths,
  unsigned max_length) -> void
{
  wide sum;
  wide const capacity = one_shifted(max_length);
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    if (frequencies.at(index) == 0) { continue; }
    add_wide(sum, one_shifted(max_length - lengths.at(index)));
  }
  REQUIRE(sum == capacity);
}

// Optimal cost of a Huffman tree equals the sum of the internal-node frequencies.
// Ties prefer the higher symbol, the opposite of the library order, so a matching
// cost is an independent check rather than a second copy of the same merges.
[[nodiscard]] auto reference_weighted_path_length(dacoexec::huffman::frequency_table const &frequencies)
  -> std::uint64_t
{
  struct item
  {
    std::uint64_t frequency_ = 0;
    std::uint32_t sequence_ = 0;
  };

  std::vector<item> nodes;
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    if (frequencies.at(index) == 0) { continue; }
    nodes.push_back(item{ .frequency_ = frequencies.at(index), .sequence_ = static_cast<std::uint32_t>(index) });
  }
  if (nodes.empty()) { return 0; }
  if (nodes.size() == 1) { return nodes.at(0).frequency_; }

  auto const lighter = [](item const &candidate, item const &current) -> bool {
    if (candidate.frequency_ != current.frequency_) { return candidate.frequency_ < current.frequency_; }
    return candidate.sequence_ > current.sequence_;
  };
  auto take_lightest = [&]() -> item {
    std::size_t best = 0;
    for (std::size_t index = 1; index < nodes.size(); ++index) {
      if (lighter(nodes.at(index), nodes.at(best))) { best = index; }
    }
    item const chosen = nodes.at(best);
    nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(best));
    return chosen;
  };

  std::uint64_t path = 0;
  auto merge_id = static_cast<std::uint32_t>(dacoexec::huffman::alphabet_size);
  while (nodes.size() > 1) {
    item const left = take_lightest();
    item const right = take_lightest();
    REQUIRE(left.frequency_ <= std::numeric_limits<std::uint64_t>::max() - right.frequency_);
    std::uint64_t const combined = left.frequency_ + right.frequency_;
    REQUIRE(path <= std::numeric_limits<std::uint64_t>::max() - combined);
    path += combined;
    nodes.push_back(item{ .frequency_ = combined, .sequence_ = merge_id });
    ++merge_id;
  }
  return path;
}

[[nodiscard]] auto weighted_path_length(dacoexec::huffman::frequency_table const &frequencies,
  dacoexec::huffman::code_length_table const &lengths) -> std::uint64_t
{
  std::uint64_t total = 0;
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    std::uint64_t const weight = frequencies.at(index);
    std::uint64_t const length = lengths.at(index);
    if (weight == 0 || length == 0) { continue; }
    REQUIRE(weight <= std::numeric_limits<std::uint64_t>::max() / length);
    REQUIRE(total <= std::numeric_limits<std::uint64_t>::max() - (weight * length));
    total += weight * length;
  }
  return total;
}

// Heavier symbols cannot require longer paths than lighter symbols.
auto check_frequency_order(dacoexec::huffman::frequency_table const &frequencies,
  dacoexec::huffman::code_length_table const &lengths) -> void
{
  for (std::size_t left = 0; left < dacoexec::huffman::alphabet_size; ++left) {
    if (frequencies.at(left) == 0) { continue; }
    for (std::size_t right = 0; right < dacoexec::huffman::alphabet_size; ++right) {
      if (frequencies.at(right) == 0) { continue; }
      if (frequencies.at(left) > frequencies.at(right)) { REQUIRE(lengths.at(left) <= lengths.at(right)); }
    }
  }
}

auto check_code_lengths(dacoexec::huffman::frequency_table const &frequencies) -> void
{
  auto const original = frequencies;
  auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
  auto const repeated = dacoexec::huffman::build_code_lengths(frequencies);
  REQUIRE(frequencies == original);
  REQUIRE(lengths == repeated);

  unsigned present = 0;
  unsigned max_length = 0;
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    if (frequencies.at(index) == 0) {
      REQUIRE(lengths.at(index) == 0);
      continue;
    }
    REQUIRE(lengths.at(index) >= 1);
    ++present;
    max_length = std::max(max_length, static_cast<unsigned>(lengths.at(index)));
  }

  if (present == 0) {
    REQUIRE(lengths == dacoexec::huffman::code_length_table{});
    REQUIRE(weighted_path_length(frequencies, lengths) == 0);
    return;
  }

  unsigned at_max = 0;
  unsigned length_one = 0;
  unsigned length_two = 0;
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    if (frequencies.at(index) == 0) { continue; }
    if (lengths.at(index) == max_length) { ++at_max; }
    if (lengths.at(index) == 1) { ++length_one; }
    if (lengths.at(index) == 2) { ++length_two; }
  }

  if (present == 1) {
    REQUIRE(max_length == 1);
    REQUIRE(length_one == 1);
  } else {
    REQUIRE(max_length <= present - 1U);
    REQUIRE(at_max >= 2);
    check_kraft(frequencies, lengths, max_length);
  }
  if (present == 2) {
    REQUIRE(length_one == 2);
    REQUIRE(max_length == 1);
  }
  if (present == 3) {
    REQUIRE(length_one == 1);
    REQUIRE(length_two == 2);
  }

  check_frequency_order(frequencies, lengths);

  REQUIRE(weighted_path_length(frequencies, lengths) == reference_weighted_path_length(frequencies));
}

struct histogram_limits
{
  std::size_t active_symbols_;
  std::uint64_t max_frequency_;
};

[[nodiscard]] auto random_histogram(splitmix64 &rng, histogram_limits limits) -> dacoexec::huffman::frequency_table
{
  dacoexec::huffman::frequency_table frequencies{};
  std::array<std::size_t, dacoexec::huffman::alphabet_size> symbols{};
  for (std::size_t index = 0; index < symbols.size(); ++index) { symbols.at(index) = index; }
  std::size_t const count = std::min(limits.active_symbols_, dacoexec::huffman::alphabet_size);
  for (std::size_t index = 0; index < count; ++index) {
    std::size_t const span = dacoexec::huffman::alphabet_size - index;
    std::size_t const chosen = index + (rng.next() % span);
    std::swap(symbols.at(index), symbols.at(chosen));
    frequencies.at(symbols.at(index)) = 1 + (rng.next() % limits.max_frequency_);
  }
  return frequencies;
}

[[nodiscard]] auto chain_frequencies(std::size_t count) -> dacoexec::huffman::frequency_table
{
  dacoexec::huffman::frequency_table frequencies{};
  if (count == 0) { return frequencies; }
  frequencies.at(0) = 1;
  if (count == 1) { return frequencies; }
  frequencies.at(1) = 1;
  for (std::size_t index = 2; index < count; ++index) { frequencies.at(index) = std::uint64_t{ 1 } << (index - 1U); }
  return frequencies;
}

template<class Byte> auto require_extreme_byte_counts() -> void
{
  std::array<Byte, 3> const input{ {
    std::bit_cast<Byte>(static_cast<unsigned char>(0x00)),
    std::bit_cast<Byte>(static_cast<unsigned char>(0x80)),
    std::bit_cast<Byte>(static_cast<unsigned char>(0xFF)),
  } };
  if constexpr (std::signed_integral<Byte>) { REQUIRE(input.at(2) < 0); }
  auto const frequencies = dacoexec::huffman::count_frequencies(input);
  REQUIRE(frequencies.at(0x00) == 1);
  REQUIRE(frequencies.at(0x80) == 1);
  REQUIRE(frequencies.at(0xFF) == 1);
  REQUIRE(frequencies.at(0x7F) == 0);
}

}// namespace

struct huffman_byte_proxy
{
  std::byte value_{};
  // Implicit conversion is required by indirectly_readable.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  constexpr operator std::byte() const { return value_; }
};

template<template<class> class QualByte, template<class> class QualProxy>
struct std::basic_common_reference<std::byte, huffman_byte_proxy, QualByte, QualProxy>
{
  using type = std::byte;
};

template<template<class> class QualProxy, template<class> class QualByte>
struct std::basic_common_reference<huffman_byte_proxy, std::byte, QualProxy, QualByte>
{
  using type = std::byte;
};

class huffman_proxy_bytes : public std::ranges::view_interface<huffman_proxy_bytes>
{
public:
  explicit huffman_proxy_bytes(std::vector<std::byte> data) : data_(std::move(data)) {}

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;

    [[nodiscard]] auto operator*() const -> huffman_byte_proxy { return huffman_byte_proxy{ *pointer_ }; }

    auto operator++() -> iterator &
    {
      ++pointer_;
      return *this;
    }

    auto operator++([[maybe_unused]] int postfix) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor,
      [[maybe_unused]] std::default_sentinel_t sentinel) noexcept -> bool
    { return cursor.pointer_ == cursor.last_; }

  private:
    friend class huffman_proxy_bytes;

    iterator(std::vector<std::byte>::const_iterator pointer, std::vector<std::byte>::const_iterator last)
      : pointer_(pointer), last_(last)
    {}

    std::vector<std::byte>::const_iterator pointer_;
    std::vector<std::byte>::const_iterator last_;
  };

  [[nodiscard]] auto begin() -> iterator { return iterator{ data_.cbegin(), data_.cend() }; }

  [[nodiscard]] static auto end() noexcept -> std::default_sentinel_t { return std::default_sentinel; }

private:
  std::vector<std::byte> data_;
};

static_assert(std::input_iterator<huffman_proxy_bytes::iterator>);
static_assert(dacoexec::byte_input_range<huffman_proxy_bytes>);
static_assert(std::same_as<std::ranges::range_value_t<huffman_proxy_bytes>, std::byte>);
static_assert(std::same_as<std::iter_reference_t<huffman_proxy_bytes::iterator>, huffman_byte_proxy>);

TEST_CASE("empty input has an all-zero frequency table", "[huffman]")
{
  REQUIRE(dacoexec::huffman::count_frequencies(std::string_view{}) == dacoexec::huffman::frequency_table{});
  REQUIRE(dacoexec::huffman::count_frequencies(std::vector<std::byte>{}) == dacoexec::huffman::frequency_table{});
}

TEST_CASE("a repeated symbol is counted", "[huffman]")
{
  std::vector<char> const input(100, 'Q');
  auto const frequencies = dacoexec::huffman::count_frequencies(input);
  REQUIRE(frequencies.at(symbol('Q')) == 100);
  REQUIRE(frequencies.at(symbol('A')) == 0);
  REQUIRE(frequencies.at(0x00) == 0);
}

TEST_CASE("the full alphabet populates every counter", "[huffman]")
{
  std::vector<unsigned char> bytes(dacoexec::huffman::alphabet_size);
  for (std::size_t index = 0; index < bytes.size(); ++index) { bytes.at(index) = static_cast<unsigned char>(index); }
  bytes.push_back(0x00);
  bytes.push_back(high_bit_value);
  bytes.push_back(all_bits_value);

  auto const frequencies = dacoexec::huffman::count_frequencies(bytes);
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    std::uint64_t const expected = (index == 0x00 || index == 0x80 || index == 0xFF) ? 2 : 1;
    REQUIRE(frequencies.at(index) == expected);
  }
}

TEST_CASE("every byte element type preserves 0x00, 0x80, and 0xFF", "[huffman]")
{
  require_extreme_byte_counts<std::byte>();
  require_extreme_byte_counts<char>();
  require_extreme_byte_counts<unsigned char>();
  require_extreme_byte_counts<signed char>();
  require_extreme_byte_counts<char8_t>();
}

TEST_CASE("a move-only unsized source is read once", "[huffman]")
{
  std::vector<std::byte> const data{
    std::byte{ 0x00 },
    std::byte{ 0x80 },
    std::byte{ 0xFF },
    std::byte{ 0x00 },
  };
  auto stats = std::make_shared<source_stats>();
  byte_source source{ data, stats };

  auto const frequencies = dacoexec::huffman::count_frequencies(source);
  REQUIRE(stats->begin_calls_ == 1);
  REQUIRE(stats->dereferences_ == data.size());
  REQUIRE(stats->increments_ == data.size());
  REQUIRE(frequencies == dacoexec::huffman::count_frequencies(data));
  REQUIRE(frequencies.at(0x00) == 2);
  REQUIRE(frequencies.at(0x80) == 1);
  REQUIRE(frequencies.at(0xFF) == 1);

  auto const remainder = dacoexec::huffman::count_frequencies(source);
  REQUIRE(remainder == dacoexec::huffman::frequency_table{});
  REQUIRE(stats->begin_calls_ == 2);
  REQUIRE(stats->dereferences_ == data.size());
}

TEST_CASE("a proxy reference is materialized as its byte value", "[huffman]")
{
  std::vector<std::byte> const data{
    std::byte{ 0x00 },
    std::byte{ 0x80 },
    std::byte{ 0xFF },
    std::byte{ 0x80 },
  };
  auto const frequencies = dacoexec::huffman::count_frequencies(huffman_proxy_bytes{ data });
  REQUIRE(frequencies.at(0x00) == 1);
  REQUIRE(frequencies.at(0x80) == 2);
  REQUIRE(frequencies.at(0xFF) == 1);
}

TEST_CASE("a throwing input iterator propagates its exception", "[huffman]")
{
  std::vector<std::byte> const data{ std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 } };
  auto stats = std::make_shared<source_stats>();
  stats->fail_at_dereference_ = 3;
  byte_source source{ data, stats };
  dacoexec::huffman::frequency_table frequencies{};

  REQUIRE_THROWS_AS(dacoexec::huffman::accumulate_frequencies(frequencies, source), injected_read_error);
  REQUIRE(frequencies.at(0x01) == 1);
  REQUIRE(frequencies.at(0x02) == 1);
  REQUIRE(frequencies.at(0x03) == 0);
  REQUIRE(stats->dereferences_ == 3);
  REQUIRE(stats->increments_ == 2);
}

TEST_CASE("frequency counters reject overflow and keep earlier increments", "[huffman]")
{
  dacoexec::huffman::frequency_table frequencies{};
  frequencies.at(0x00) = std::numeric_limits<std::uint64_t>::max();
  std::array<unsigned char, 2> const input{ { 0x11, 0x00 } };

  try {
    dacoexec::huffman::accumulate_frequencies(frequencies, input);
    FAIL("expected frequency overflow");
  } catch (std::overflow_error const &error) {
    REQUIRE(std::string_view{ error.what() } == "Huffman frequency counter overflow");
  }
  REQUIRE(frequencies.at(0x11) == 1);
  REQUIRE(frequencies.at(0x00) == std::numeric_limits<std::uint64_t>::max());
  REQUIRE(frequencies.at(0x01) == 0);
}

TEST_CASE("accumulated ranges match the concatenated input", "[huffman]")
{
  dacoexec::huffman::frequency_table frequencies{};
  dacoexec::huffman::accumulate_frequencies(frequencies, std::string_view{ "ABCA" });
  dacoexec::huffman::accumulate_frequencies(frequencies, std::string_view{ "AAB" });
  REQUIRE(frequencies == dacoexec::huffman::count_frequencies(std::string_view{ "ABCAAAB" }));
}

TEST_CASE("the same bytes in a different order share a histogram", "[huffman]")
{
  auto const forward = dacoexec::huffman::count_frequencies(std::string_view{ "AAAAABBCD" });
  auto const reverse = dacoexec::huffman::count_frequencies(std::string_view{ "DCBBAAAAA" });
  REQUIRE(forward == reverse);
  REQUIRE(forward.at(symbol('A')) == 5);
  REQUIRE(forward.at(symbol('B')) == 2);
  REQUIRE(forward.at(symbol('C')) == 1);
  REQUIRE(forward.at(symbol('D')) == 1);
}

TEST_CASE("an owned block is counted through its byte buffer", "[huffman]")
{
  std::string_view const text = "AAAAABBCD";
  std::vector<std::byte> bytes;
  bytes.reserve(text.size());
  for (char const character : text) { bytes.push_back(dacoexec::detail::to_byte(character)); }
  dacoexec::byte_block block{ 0, bytes };
  REQUIRE(dacoexec::huffman::count_frequencies(block.bytes_) == dacoexec::huffman::count_frequencies(text));
}

TEST_CASE("the documented sample has the canonical fixture lengths", "[huffman]")
{
  auto const counted = dacoexec::huffman::count_frequencies(std::string_view{ "AAAAABBCD" });
  dacoexec::huffman::frequency_table manual{};
  manual.at(symbol('A')) = sample_a_count;
  manual.at(symbol('B')) = 2;
  manual.at(symbol('C')) = 1;
  manual.at(symbol('D')) = 1;
  REQUIRE(counted == manual);

  auto const lengths = dacoexec::huffman::build_code_lengths(manual);
  REQUIRE(lengths.at(symbol('A')) == 1);
  REQUIRE(lengths.at(symbol('B')) == 2);
  REQUIRE(lengths.at(symbol('C')) == 3);
  REQUIRE(lengths.at(symbol('D')) == 3);
  REQUIRE(lengths.at(0x00) == 0);
  REQUIRE(weighted_path_length(manual, lengths) == 15);
  check_code_lengths(manual);

  dacoexec::huffman::frequency_table binary{};
  binary.at(0x00) = sample_a_count;
  binary.at(0x01) = 2;
  binary.at(high_bit_value) = 1;
  binary.at(all_bits_value) = 1;
  auto const binary_lengths = dacoexec::huffman::build_code_lengths(binary);
  REQUIRE(binary_lengths.at(0x00) == 1);
  REQUIRE(binary_lengths.at(0x01) == 2);
  REQUIRE(binary_lengths.at(0x80) == 3);
  REQUIRE(binary_lengths.at(0xFF) == 3);
  check_code_lengths(binary);
}

TEST_CASE("empty, single-symbol, and two-symbol tables have reserved lengths", "[huffman]")
{
  dacoexec::huffman::frequency_table const empty{};
  check_code_lengths(empty);

  dacoexec::huffman::frequency_table one{};
  one.at(symbol('A')) = singleton_frequency;
  auto const one_lengths = dacoexec::huffman::build_code_lengths(one);
  REQUIRE(one_lengths.at(symbol('A')) == 1);
  REQUIRE(one_lengths.at(symbol('B')) == 0);
  check_code_lengths(one);

  one.at(symbol('A')) = std::numeric_limits<std::uint64_t>::max();
  REQUIRE(dacoexec::huffman::build_code_lengths(one).at(symbol('A')) == 1);

  dacoexec::huffman::frequency_table two{};
  two.at(symbol('A')) = 1;
  two.at(symbol('Z')) = heavy_frequency;
  auto const two_lengths = dacoexec::huffman::build_code_lengths(two);
  REQUIRE(two_lengths.at(symbol('A')) == 1);
  REQUIRE(two_lengths.at(symbol('Z')) == 1);
  check_code_lengths(two);
}

TEST_CASE("equal frequencies use the lowest symbol as the tie break", "[huffman]")
{
  dacoexec::huffman::frequency_table three{};
  three.at(symbol('A')) = 1;
  three.at(symbol('B')) = 1;
  three.at(symbol('C')) = 1;
  auto const three_lengths = dacoexec::huffman::build_code_lengths(three);
  REQUIRE(three_lengths.at(symbol('A')) == 2);
  REQUIRE(three_lengths.at(symbol('B')) == 2);
  REQUIRE(three_lengths.at(symbol('C')) == 1);
  check_code_lengths(three);

  dacoexec::huffman::frequency_table four{};
  four.at(symbol('A')) = 1;
  four.at(symbol('B')) = 1;
  four.at(symbol('C')) = 1;
  four.at(symbol('D')) = 1;
  auto const four_lengths = dacoexec::huffman::build_code_lengths(four);
  REQUIRE(four_lengths.at(symbol('A')) == 2);
  REQUIRE(four_lengths.at(symbol('B')) == 2);
  REQUIRE(four_lengths.at(symbol('C')) == 2);
  REQUIRE(four_lengths.at(symbol('D')) == 2);
  check_code_lengths(four);
}

TEST_CASE("a uniform alphabet receives length 8", "[huffman]")
{
  dacoexec::huffman::frequency_table frequencies{};
  frequencies.fill(1);
  auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
  for (std::uint8_t length : lengths) { REQUIRE(length == 8); }
  check_code_lengths(frequencies);

  dacoexec::huffman::frequency_table scaled{};
  scaled.fill(frequency_scale);
  REQUIRE(dacoexec::huffman::build_code_lengths(scaled) == lengths);
}

TEST_CASE("a strictly heavier symbol receives a shorter or equal code", "[huffman]")
{
  dacoexec::huffman::frequency_table frequencies{};
  frequencies.at(0) = 1;
  frequencies.at(1) = 1;
  frequencies.at(last_symbol) = heavy_frequency;
  auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
  REQUIRE(lengths.at(255) <= lengths.at(0));
  REQUIRE(lengths.at(255) <= lengths.at(1));
  REQUIRE(lengths.at(255) == 1);
  check_code_lengths(frequencies);
}

TEST_CASE("a skewed chain keeps lengths above 32 bits", "[huffman]")
{
  auto const small = chain_frequencies(5);
  auto const small_lengths = dacoexec::huffman::build_code_lengths(small);
  REQUIRE(small_lengths.at(0) == 4);
  REQUIRE(small_lengths.at(1) == 4);
  REQUIRE(small_lengths.at(2) == 3);
  REQUIRE(small_lengths.at(3) == 2);
  REQUIRE(small_lengths.at(4) == 1);
  REQUIRE(small_lengths.at(5) == 0);
  check_code_lengths(small);

  constexpr std::size_t chain_symbols = 40;
  auto const frequencies = chain_frequencies(chain_symbols);
  auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
  REQUIRE(lengths.at(0) == 39);
  REQUIRE(lengths.at(1) == 39);
  REQUIRE(lengths.at(2) == 38);
  REQUIRE(lengths.at(chain_symbols - 1) == 1);
  REQUIRE(lengths.at(chain_symbols) == 0);
  REQUIRE(lengths.at(0) > 32);
  check_code_lengths(frequencies);
}

TEST_CASE("large uint64 frequencies stay unsigned", "[huffman]")
{
  dacoexec::huffman::frequency_table frequencies{};
  frequencies.at(0) = std::uint64_t{ 1 } << high_frequency_shift;
  frequencies.at(1) = (std::uint64_t{ 1 } << high_frequency_shift) - 1U;
  auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
  REQUIRE(lengths.at(0) == 1);
  REQUIRE(lengths.at(1) == 1);
  check_code_lengths(frequencies);

  dacoexec::huffman::frequency_table triple{};
  triple.at(0) = std::uint64_t{ 1 } << quarter_frequency_shift;
  triple.at(1) = std::uint64_t{ 1 } << quarter_frequency_shift;
  triple.at(2) = std::uint64_t{ 1 } << quarter_frequency_shift;
  auto const triple_lengths = dacoexec::huffman::build_code_lengths(triple);
  REQUIRE(triple_lengths.at(0) == 2);
  REQUIRE(triple_lengths.at(1) == 2);
  REQUIRE(triple_lengths.at(2) == 1);
  // The weighted cost is 5 * 2^62, which does not fit in uint64_t. The parent
  // sums themselves stay inside the counter, which is what this case checks.
}

TEST_CASE("a parent frequency that overflows uint64 is rejected", "[huffman]")
{
  auto require_overflow = [](dacoexec::huffman::frequency_table const &frequencies) -> void {
    try {
      auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
      static_cast<void>(lengths);
      FAIL("expected tree frequency overflow");
    } catch (std::overflow_error const &error) {
      REQUIRE(std::string_view{ error.what() } == "Huffman tree frequency overflow");
    }
  };

  dacoexec::huffman::frequency_table direct{};
  direct.at(0) = std::numeric_limits<std::uint64_t>::max();
  direct.at(1) = 1;
  require_overflow(direct);

  dacoexec::huffman::frequency_table indirect{};
  indirect.at(0) = 1;
  indirect.at(1) = 1;
  indirect.at(2) = std::numeric_limits<std::uint64_t>::max();
  require_overflow(indirect);

  dacoexec::huffman::frequency_table boundary{};
  boundary.at(0) = std::numeric_limits<std::uint64_t>::max() - 1U;
  boundary.at(1) = 1;
  auto const lengths = dacoexec::huffman::build_code_lengths(boundary);
  REQUIRE(lengths.at(0) == 1);
  REQUIRE(lengths.at(1) == 1);
}

TEST_CASE("tree construction ignores table fill order", "[huffman]")
{
  dacoexec::huffman::frequency_table forward{};
  dacoexec::huffman::frequency_table reverse{};
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    std::uint64_t const frequency = (index * 17U) % 11U;
    forward.at(index) = frequency;
  }
  for (std::size_t step = 0; step < dacoexec::huffman::alphabet_size; ++step) {
    std::size_t const index = dacoexec::huffman::alphabet_size - 1U - step;
    reverse.at(index) = (index * histogram_multiplier) % histogram_modulus;
  }
  REQUIRE(forward == reverse);

  auto const first = dacoexec::huffman::build_code_lengths(forward);
  auto const second = dacoexec::huffman::build_code_lengths(forward);
  auto const third = dacoexec::huffman::build_code_lengths(reverse);
  REQUIRE(first == second);
  REQUIRE(first == third);
  check_code_lengths(forward);
}

TEST_CASE("every four-symbol histogram in 0..3 has the optimal cost", "[huffman]")
{
  for (std::uint64_t first = 0; first < 4; ++first) {
    for (std::uint64_t second = 0; second < 4; ++second) {
      for (std::uint64_t third = 0; third < 4; ++third) {
        for (std::uint64_t fourth = 0; fourth < 4; ++fourth) {
          INFO(first << " " << second << " " << third << " " << fourth);
          dacoexec::huffman::frequency_table frequencies{};
          frequencies.at(0) = first;
          frequencies.at(1) = second;
          frequencies.at(2) = third;
          frequencies.at(3) = fourth;
          check_code_lengths(frequencies);
        }
      }
    }
  }
}

TEST_CASE("seeded histograms satisfy the length invariants", "[huffman]")
{
  splitmix64 rng{ histogram_seed };
  for (int index = 0; index < small_histogram_cases; ++index) {
    INFO("small histogram " << index);
    auto const frequencies = random_histogram(rng, { .active_symbols_ = rng.next() % 17U, .max_frequency_ = 8 });
    check_code_lengths(frequencies);
  }
  for (int index = 0; index < wide_histogram_cases; ++index) {
    INFO("wide histogram " << index);
    auto const frequencies = random_histogram(rng, { .active_symbols_ = rng.next() % 257U, .max_frequency_ = 5 });
    check_code_lengths(frequencies);
  }
}

TEST_CASE("canonical codebooks reconstruct deterministic fixtures", "[huffman][canonical]")
{
  constexpr std::array samples{ std::string_view{},
    std::string_view{ "A" },
    std::string_view{ "AB" },
    std::string_view{ "AAAAABBCD" },
    std::string_view{ "ABCD" } };
  for (auto const input : samples) {
    auto const lengths = dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(input));
    auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
    auto const saved = book.lengths();
    auto const rebuilt = dacoexec::huffman::make_canonical_codebook(saved);
    REQUIRE(saved == lengths);
    for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
      auto const byte = static_cast<std::uint8_t>(index);
      REQUIRE(test_detail::canonical_word(book, byte) == test_detail::canonical_word(rebuilt, byte));
      REQUIRE(test_detail::canonical_word(book, byte).length_ == lengths.at(index));
    }
  }
  auto const singleton = dacoexec::huffman::make_canonical_codebook(
    dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(std::string_view{ "A" })));
  REQUIRE(
    test_detail::canonical_word(singleton, symbol('A')) == dacoexec::huffman::codeword{ .bits_ = 0U, .length_ = 1 });
  auto const pair = dacoexec::huffman::make_canonical_codebook(
    dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(std::string_view{ "BA" })));
  REQUIRE(test_detail::canonical_word(pair, symbol('A')) == dacoexec::huffman::codeword{ .bits_ = 0U, .length_ = 1 });
  REQUIRE(test_detail::canonical_word(pair, symbol('B')) == dacoexec::huffman::codeword{ .bits_ = 1U, .length_ = 1 });
  auto const book = dacoexec::huffman::make_canonical_codebook(
    dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(std::string_view{ "AAAAABBCD" })));
  REQUIRE(test_detail::canonical_word(book, symbol('A')) == dacoexec::huffman::codeword{ .bits_ = 0b0U, .length_ = 1 });
  REQUIRE(
    test_detail::canonical_word(book, symbol('B')) == dacoexec::huffman::codeword{ .bits_ = 0b10U, .length_ = 2 });
  REQUIRE(
    test_detail::canonical_word(book, symbol('C')) == dacoexec::huffman::codeword{ .bits_ = 0b110U, .length_ = 3 });
  REQUIRE(
    test_detail::canonical_word(book, symbol('D')) == dacoexec::huffman::codeword{ .bits_ = 0b111U, .length_ = 3 });
  auto const equal = dacoexec::huffman::make_canonical_codebook(
    dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(std::string_view{ "DCBA" })));
  constexpr unsigned equal_length = 2;
  constexpr std::uint8_t equal_symbols = 4;
  for (std::uint8_t index = 0; index < equal_symbols; ++index) {
    REQUIRE(test_detail::canonical_word(equal, static_cast<std::uint8_t>(symbol('A') + index))
            == dacoexec::huffman::codeword{ .bits_ = index, .length_ = equal_length });
  }
}

TEST_CASE("canonical validation rejects invalid trees and reports unsupported widths", "[huffman][canonical]")
{
  dacoexec::huffman::code_length_table lengths{};
  lengths.at(0) = 2;
  REQUIRE_THROWS_AS(dacoexec::huffman::make_canonical_codebook(lengths), std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::huffman::try_make_canonical_codebook(lengths), std::invalid_argument);
  lengths.at(1) = 2;
  REQUIRE_THROWS_AS(dacoexec::huffman::make_canonical_codebook(lengths), std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::huffman::try_make_canonical_codebook(lengths), std::invalid_argument);
  lengths.at(0) = 1;
  lengths.at(1) = 1;
  lengths.at(2) = 1;
  REQUIRE_THROWS_AS(dacoexec::huffman::make_canonical_codebook(lengths), std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::huffman::try_make_canonical_codebook(lengths), std::invalid_argument);
  constexpr std::size_t unsupported_chain_symbols = 40;
  auto const deep = dacoexec::huffman::build_code_lengths(chain_frequencies(unsupported_chain_symbols));
  REQUIRE(*std::ranges::max_element(deep) == unsupported_chain_symbols - 1U);
  REQUIRE_FALSE(dacoexec::huffman::try_make_canonical_codebook(deep).has_value());
  REQUIRE_THROWS_AS(dacoexec::huffman::make_canonical_codebook(deep), std::length_error);
}

TEST_CASE("canonical codes preserve all unsigned byte indexes", "[huffman][canonical]")
{
  dacoexec::huffman::code_length_table lengths{};
  constexpr std::uint8_t byte_width = 8;
  lengths.fill(byte_width);
  auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    REQUIRE(test_detail::canonical_word(book, static_cast<std::uint8_t>(index))
            == dacoexec::huffman::codeword{ .bits_ = static_cast<std::uint32_t>(index), .length_ = byte_width });
  }
  constexpr std::array<unsigned char, 3> extremes{ 0x00, 0x80, 0xFF };
  auto const sparse = dacoexec::huffman::make_canonical_codebook(
    dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(extremes)));
  REQUIRE(test_detail::canonical_word(sparse, 0x00) == dacoexec::huffman::codeword{ .bits_ = 0b10U, .length_ = 2 });
  REQUIRE(test_detail::canonical_word(sparse, 0x80) == dacoexec::huffman::codeword{ .bits_ = 0b11U, .length_ = 2 });
  REQUIRE(test_detail::canonical_word(sparse, 0xFF) == dacoexec::huffman::codeword{ .bits_ = 0U, .length_ = 1 });
}

TEST_CASE("canonical assignment reaches the full 32-bit boundary", "[huffman][canonical]")
{
  dacoexec::huffman::code_length_table lengths{};
  for (unsigned length = 1; length < dacoexec::huffman::max_canonical_bits; ++length) {
    lengths.at(length - 1U) = static_cast<std::uint8_t>(length);
  }
  lengths.at(dacoexec::huffman::max_canonical_bits - 1U) = dacoexec::huffman::max_canonical_bits;
  lengths.at(dacoexec::huffman::max_canonical_bits) = dacoexec::huffman::max_canonical_bits;
  auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
  REQUIRE(test_detail::canonical_word(book, dacoexec::huffman::max_canonical_bits - 1U)
          == dacoexec::huffman::codeword{ .bits_ = 0xFFFFFFFEU, .length_ = dacoexec::huffman::max_canonical_bits });
  REQUIRE(test_detail::canonical_word(book, dacoexec::huffman::max_canonical_bits)
          == dacoexec::huffman::codeword{ .bits_ = 0xFFFFFFFFU, .length_ = dacoexec::huffman::max_canonical_bits });

  std::vector<std::byte> output;
  dacoexec::bit_writer writer{ std::back_inserter(output) };
  for (auto const symbol : { dacoexec::huffman::max_canonical_bits - 1U, dacoexec::huffman::max_canonical_bits }) {
    auto const word = test_detail::canonical_word(book, static_cast<std::uint8_t>(symbol));
    writer.write_bits(word.bits_, word.length_);
  }
  constexpr unsigned no_padding = 0;
  REQUIRE(writer.finish() == no_padding);
  REQUIRE(output
          == std::vector<std::byte>{ std::byte{ 0xFF },
            std::byte{ 0xFF },
            std::byte{ 0xFF },
            std::byte{ 0xFE },
            std::byte{ 0xFF },
            std::byte{ 0xFF },
            std::byte{ 0xFF },
            std::byte{ 0xFF } });
}

TEST_CASE("seeded canonical codes are ordered prefix-free and reconstructible", "[huffman][canonical]")
{
  constexpr std::size_t case_count = 64;
  constexpr std::uint64_t frequency_limit = 8;
  splitmix64 rng{ histogram_seed };
  for (std::size_t index = 0; index < case_count; ++index) {
    auto const frequencies = random_histogram(rng,
      { .active_symbols_ = rng.next() % (dacoexec::huffman::alphabet_size + 1U), .max_frequency_ = frequency_limit });
    auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
    auto const candidate = dacoexec::huffman::try_make_canonical_codebook(lengths);
    if (!candidate.has_value()) {
      FAIL("Seeded Huffman lengths must fit the supported codeword width");
      continue;
    }
    auto const &book = candidate.value();
    auto const rebuilt = dacoexec::huffman::make_canonical_codebook(book.lengths());
    for (std::size_t left = 0; left < dacoexec::huffman::alphabet_size; ++left) {
      auto const word = test_detail::canonical_word(book, static_cast<std::uint8_t>(left));
      REQUIRE(word == test_detail::canonical_word(rebuilt, static_cast<std::uint8_t>(left)));
      REQUIRE(word.length_ == lengths.at(left));
      REQUIRE((word.length_ == 0) == (frequencies.at(left) == 0));
      REQUIRE((static_cast<std::uint64_t>(word.bits_) >> word.length_) == 0);
      if (word.length_ == 0) { continue; }
      for (std::size_t right = left + 1U; right < dacoexec::huffman::alphabet_size; ++right) {
        auto const other = test_detail::canonical_word(book, static_cast<std::uint8_t>(right));
        if (other.length_ == 0) { continue; }
        if (word.length_ == other.length_) { REQUIRE(word.bits_ < other.bits_); }
        auto const shorter = word.length_ <= other.length_ ? word : other;
        auto const longer = word.length_ <= other.length_ ? other : word;
        REQUIRE((static_cast<std::uint64_t>(longer.bits_) >> static_cast<unsigned>(longer.length_ - shorter.length_))
                != shorter.bits_);
      }
    }
  }
}


TEST_CASE("unsupported canonical widths take precedence over invalid tree structure", "[huffman][canonical]")
{
  dacoexec::huffman::code_length_table lengths{};
  lengths.at(0) = 1;
  lengths.at(1) = 1;
  lengths.at(2) = 1;
  REQUIRE_THROWS_AS(dacoexec::huffman::try_make_canonical_codebook(lengths), std::invalid_argument);
  // Keep the unsupported symbol last so earlier counts already oversubscribe.
  lengths.back() = static_cast<std::uint8_t>(dacoexec::huffman::max_canonical_bits + 1U);
  REQUIRE_FALSE(dacoexec::huffman::try_make_canonical_codebook(lengths).has_value());
  REQUIRE_THROWS_AS(dacoexec::huffman::make_canonical_codebook(lengths), std::length_error);
}
