#include "error_test_utils.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/range_decoder.hpp>
#include <dacoexec/detail/arithmetic/range_encoder.hpp>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <vector>

// Literals are explicit model intervals, golden bytes, and malformed inputs.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace arithmetic = dacoexec::detail::arithmetic;

auto bytes(std::initializer_list<unsigned> values) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  for (auto value : values) { result.push_back(static_cast<std::byte>(value)); }
  return result;
}

struct interval
{
  std::uint32_t cumulative_;
  std::uint32_t frequency_;
  std::uint32_t total_;
};

auto encode(std::span<interval const> intervals) -> std::vector<std::byte>
{
  arithmetic::range_encoder encoder;
  for (auto [cumulative, frequency, total] : intervals) { encoder.encode(cumulative, frequency, total); }
  return encoder.finish();
}

void check_intervals(std::span<interval const> intervals, std::span<std::byte const> payload)
{
  arithmetic::range_decoder decoder(payload);
  for (auto [cumulative, frequency, total] : intervals) {
    auto const slot = decoder.cumulative(total);
    REQUIRE(slot >= cumulative);
    REQUIRE(slot - cumulative < frequency);
    REQUIRE(decoder.cumulative(total) == slot);
    decoder.decode(cumulative, frequency, total);
  }
  decoder.finish();
}

auto make_intervals(std::span<unsigned const> symbols, std::span<std::uint32_t const> cumulative)
  -> std::vector<interval>
{
  std::vector<interval> result;
  for (auto symbol : symbols) {
    result.push_back({ cumulative[symbol], cumulative[symbol + 1] - cumulative[symbol], cumulative.back() });
  }
  return result;
}
}// namespace

TEST_CASE("Range engine golden payloads", "[arithmetic][range]")
{
  // Independently calculated using an unbounded integer lower endpoint:
  // low += (range / total) * cumulative; on normalization low <<= 8.
  // Serialize low big endian in (normalization count + 5) bytes at the end.
  auto check = [](std::span<interval const> intervals, std::vector<std::byte> const &expected) {
    REQUIRE(encode(intervals) == expected);
    REQUIRE(encode(intervals) == expected);
    check_intervals(intervals, expected);
  };
  check({}, bytes({ 0, 0, 0, 0, 0 }));
  std::array const binary_cumulative{ 0U, 1U, 2U };
  std::array const binary_symbols{ 0U, 1U, 1U, 0U, 1U, 0U, 0U, 1U };
  check(make_intervals(binary_symbols, binary_cumulative), bytes({ 0, 0x68, 0xff, 0xff, 0xfc, 0 }));
  std::array const skew_cumulative{ 0U, 3U, 5U, 6U };
  std::array const skew_symbols{ 0U, 1U, 0U, 0U, 2U, 1U, 2U, 0U, 1U, 0U, 0U, 0U };
  check(make_intervals(skew_symbols, skew_cumulative), bytes({ 0, 0x4a, 0x4b, 0xda, 9, 0x7c, 0 }));
  std::vector<unsigned> const ones(40, 1);
  check(make_intervals(ones, binary_cumulative), bytes({ 0, 0xff, 0xff, 0xff, 0xf6, 0xff, 0, 0, 1, 0 }));

  // Four deferred bytes receive a carry together; 0xFF bytes wrap to zero.
  std::array const carry_intervals{
    interval{ 255, 1, 256 }, interval{ 0, 1, 256 }, interval{ 0, 1, 256 }, interval{ 255, 1, 256 }, interval{ 1, 1, 2 }
  };
  check(carry_intervals, bytes({ 0, 0xff, 0, 0, 0, 0x7f, 0xff, 0, 0x80 }));
  std::array const boundary_intervals{
    interval{ 0, 1, 0x1000000 }, interval{ 0xffffff, 1, 0x1000000 }, interval{ 1, 1, 3 }
  };
  check(boundary_intervals, bytes({ 0, 0, 0, 0, 0xfe, 0xff, 0xff, 0x56, 0, 0, 0 }));
  // First update makes range exactly 2^24; it must not normalize yet.
  std::array const equality_intervals{ interval{ 0, 65536, 0xffffff }, interval{ 0, 1, 2 } };
  check(equality_intervals, bytes({ 0, 0, 0, 0, 0, 0 }));
}

TEST_CASE("Range engine decodes explicit cumulative tables", "[arithmetic][range]")
{
  std::mt19937 random(42);
  std::vector<std::vector<std::uint32_t>> const tables{
    { 0, 1 }, { 0, 1, 2 }, { 0, 3, 5, 6 }, { 0, 0, 1, 1, 4096 }, { 0, 1, 32768 }, { 0, 1, 0xffffff, 0x1000000 }
  };
  for (auto const &table : tables) {
    CAPTURE(table);
    std::vector<unsigned> symbols;
    for (unsigned index = 0; index < 20000; ++index) {
      auto const slot = random() % table.back();
      auto const symbol = std::upper_bound(table.begin(), table.end(), slot) - table.begin() - 1;
      symbols.push_back(static_cast<unsigned>(symbol));
    }
    auto const intervals = make_intervals(symbols, table);
    auto const payload = encode(intervals);
    REQUIRE(encode(intervals) == payload);
    arithmetic::range_decoder decoder(payload);
    for (auto symbol : symbols) {
      auto const slot = decoder.cumulative(table.back());
      auto const decoded = std::upper_bound(table.begin(), table.end(), slot) - table.begin() - 1;
      REQUIRE(static_cast<unsigned>(decoded) == symbol);
      decoder.decode(table[symbol], table[symbol + 1] - table[symbol], table.back());
    }
    decoder.finish();
  }

  // Exercise every byte, with an explicitly supplied uniform 256-symbol table.
  std::array<std::uint32_t, 257> table{};
  for (std::size_t index = 0; index < table.size(); ++index) { table[index] = static_cast<std::uint32_t>(index); }
  std::vector<unsigned> symbols;
  for (unsigned index = 0; index < 65536; ++index) { symbols.push_back(index % 256); }
  auto const intervals = make_intervals(symbols, table);
  check_intervals(intervals, encode(intervals));
}

TEST_CASE("Range engine supports changing totals and extreme intervals", "[arithmetic][range]")
{
  std::mt19937 random(123);
  std::array const totals{ 1U, 2U, 3U, 256U, 4096U, 32768U, 0x1000000U };
  std::vector<interval> intervals;
  for (unsigned index = 0; index < 10000; ++index) {
    auto const total = totals[index % totals.size()];
    // Alternate unit-width extremes and arbitrary positive intervals.
    auto const frequency = index % 2 == 0 ? 1U : 1U + static_cast<std::uint32_t>(random() % total);
    auto const cumulative =
      index % 2 == 0 ? total - frequency : static_cast<std::uint32_t>(random() % (total - frequency + 1));
    intervals.push_back({ cumulative, frequency, total });
  }
  auto const payload = encode(intervals);
  check_intervals(intervals, payload);
  REQUIRE(encode(intervals) == payload);
}

TEST_CASE("Range engine rejects invalid intervals and finalized use", "[arithmetic][range]")
{
  arithmetic::range_encoder encoder;
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_argument);
  std::array const invalid_intervals{ interval{ 0, 1, 0 },
    interval{ 0, 0, 2 },
    interval{ 2, 1, 2 },
    interval{ 1, 2, 2 },
    interval{ 0, 1, 0x1000001 },
    interval{ 1, std::numeric_limits<std::uint32_t>::max(), 2 } };
  auto const empty = bytes({ 0, 0, 0, 0, 0 });
  arithmetic::range_decoder decoder(empty);
  for (auto [cumulative, frequency, total] : invalid_intervals) {
    REQUIRE_THROWS_MATCHES(encoder.encode(cumulative, frequency, total), dacoexec::exception, invalid);
    REQUIRE_THROWS_MATCHES(decoder.decode(cumulative, frequency, total), dacoexec::exception, invalid);
  }
  REQUIRE_THROWS_MATCHES(decoder.cumulative(0), dacoexec::exception, invalid);
  REQUIRE_THROWS_MATCHES(decoder.cumulative(0x1000001), dacoexec::exception, invalid);
  REQUIRE(encoder.finish() == empty);
  decoder.finish();
  auto const finished = dacoexec::test::error_is(dacoexec::errc::invalid_state);
  REQUIRE_THROWS_MATCHES(encoder.finish(), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(encoder.encode(0, 1, 2), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(decoder.finish(), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(decoder.cumulative(2), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(decoder.decode(0, 1, 2), dacoexec::exception, finished);
}

TEST_CASE("Range engine rejects malformed payload and termination", "[arithmetic][range]")
{
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_payload);
  auto const payload = bytes({ 0, 0x68, 0xff, 0xff, 0xfc, 0 });
  std::array const cumulative{ 0U, 1U, 2U };
  std::array const symbols{ 0U, 1U, 1U, 0U, 1U, 0U, 0U, 1U };
  auto const intervals = make_intervals(symbols, cumulative);
  for (std::size_t size = 0; size < payload.size(); ++size) {
    REQUIRE_THROWS_MATCHES(check_intervals(intervals, std::span(payload).first(size)), dacoexec::exception, invalid);
  }
  auto trailing = payload;
  trailing.push_back(std::byte{ 0 });
  REQUIRE_THROWS_MATCHES(check_intervals(intervals, trailing), dacoexec::exception, invalid);
  auto bad_prefix = payload;
  bad_prefix.front() = std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(arithmetic::range_decoder{ bad_prefix }, dacoexec::exception, invalid);
  auto const bad_initial = bytes({ 0, 0xff, 0xff, 0xff, 0xff });
  REQUIRE_THROWS_MATCHES(arithmetic::range_decoder{ bad_initial }, dacoexec::exception, invalid);
  auto const bad_terminal = bytes({ 0, 0, 0, 0, 1 });
  arithmetic::range_decoder terminal(bad_terminal);
  REQUIRE_THROWS_MATCHES(terminal.finish(), dacoexec::exception, invalid);
  arithmetic::range_decoder wrong_interval(payload);
  REQUIRE_THROWS_MATCHES(wrong_interval.decode(1, 1, 2), dacoexec::exception, invalid);
  // Valid initial code in the division remainder, outside all symbol intervals.
  auto const remainder = bytes({ 0, 0xff, 0xff, 0xff, 0xfe });
  arithmetic::range_decoder outside(remainder);
  REQUIRE_THROWS_MATCHES(outside.cumulative(2), dacoexec::exception, invalid);
  REQUIRE_THROWS_MATCHES(outside.decode(1, 1, 2), dacoexec::exception, invalid);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
