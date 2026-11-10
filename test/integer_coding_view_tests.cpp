#include "error_test_utils.hpp"
#include <dacoexec/bit.hpp>
#include <dacoexec/detail/integer_coding/common.hpp>
#include <dacoexec/detail/integer_coding/delta.hpp>
#include <dacoexec/detail/integer_coding/gamma.hpp>
#include <dacoexec/detail/integer_coding/golomb.hpp>
#include <dacoexec/detail/integer_coding/rice.hpp>
#include <dacoexec/detail/views/bits.hpp>
#include <dacoexec/detail/views/integer_decode.hpp>
#include <dacoexec/detail/views/integer_encode.hpp>
#include <dacoexec/error.hpp>
#include <dacoexec/views.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

template<std::ranges::input_range R> auto collect(R &&range) -> std::vector<std::ranges::range_value_t<R>>
{
  std::vector<std::ranges::range_value_t<R>> result;
  for (auto const value : std::forward<R>(range)) { result.push_back(value); }
  return result;
}

template<std::ranges::input_range R> auto bit_string(R &&range) -> std::string
{
  std::string result;
  for (bool const bit : std::forward<R>(range)) { result.push_back(bit ? '1' : '0'); }
  return result;
}

namespace dv = dacoexec::detail::views;
namespace pv = dacoexec::views;
constexpr std::uint32_t golomb_divisor = 5;
constexpr unsigned rice_shift = 2;
constexpr auto maximum_integer = std::numeric_limits<std::uint64_t>::max();

using encode_range = decltype(std::declval<std::vector<std::uint64_t> &>() | dv::encode::gamma());
using decode_range = decltype(std::declval<encode_range>() | dv::decode::gamma(0));
static_assert(std::ranges::input_range<encode_range>);
static_assert(std::ranges::input_range<decode_range>);
static_assert(!std::ranges::forward_range<encode_range>);
static_assert(!std::ranges::forward_range<decode_range>);
static_assert(!std::copyable<encode_range>);
static_assert(!std::copyable<decode_range>);
static_assert(std::same_as<std::ranges::range_value_t<encode_range>, bool>);
static_assert(std::same_as<std::ranges::range_value_t<decode_range>, std::uint64_t>);

template<typename R, typename Adaptor>
concept mapping_input = requires(R &&range, Adaptor adaptor) { std::forward<R>(range) | adaptor; };

using zigzag_encoder = decltype(pv::encode::zigzag());
using zigzag_decoder = decltype(pv::decode::zigzag());
using positive_encoder = decltype(pv::encode::positive());
using positive_decoder = decltype(pv::decode::positive());
static_assert(mapping_input<std::vector<int> &, zigzag_encoder>);
static_assert(mapping_input<std::vector<std::uint64_t> &, zigzag_decoder>);
static_assert(mapping_input<std::vector<unsigned> &, positive_encoder>);
static_assert(mapping_input<std::vector<unsigned> &, positive_decoder>);
static_assert(!mapping_input<std::vector<unsigned> &, zigzag_encoder>);
static_assert(!mapping_input<std::vector<int> &, zigzag_decoder>);
static_assert(!mapping_input<std::vector<int> &, positive_encoder>);
static_assert(!mapping_input<std::vector<int> &, positive_decoder>);
static_assert(!mapping_input<std::vector<bool> &, positive_encoder>);
static_assert(!mapping_input<std::vector<bool> &, zigzag_decoder>);
static_assert(!mapping_input<std::vector<double> &, zigzag_encoder>);

constexpr std::array signed_mapping_values{ 0, -1, 1, -2, 2 };
constexpr auto mapped_values = signed_mapping_values | pv::encode::zigzag();
static_assert(mapped_values[1] == 1U && mapped_values[3] == 3U);
static_assert(std::same_as<std::ranges::range_value_t<decltype(mapped_values)>, std::uint64_t>);
static_assert(std::same_as<std::ranges::range_value_t<decltype(mapped_values | pv::decode::zigzag())>, std::int64_t>);

}// namespace

TEST_CASE("public integer mappings preserve values and signed boundaries", "[integer_coding][views]")
{
  REQUIRE(collect(signed_mapping_values | pv::encode::zigzag()) == std::vector<std::uint64_t>{ 0, 1, 2, 3, 4 });
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  std::vector<std::int64_t> const values{ minimum, maximum, -1, 0, 1 };
  auto mapped = pv::encode::zigzag()(values);
  REQUIRE(*mapped.begin() == maximum_integer);
  REQUIRE(collect(pv::decode::zigzag()(mapped)) == values);
  REQUIRE(collect(std::vector<int>{ -1, 0, 1 } | pv::encode::zigzag()) == std::vector<std::uint64_t>{ 1, 0, 2 });
  std::vector<std::uint64_t> const nonnegative{ 0, 1, maximum_integer - 1U };
  REQUIRE(collect(pv::encode::positive()(nonnegative)) == std::vector<std::uint64_t>{ 1, 2, maximum_integer });
  REQUIRE(collect(nonnegative | pv::encode::positive() | pv::decode::positive()) == nonnegative);
}

TEST_CASE("public mappings compose with integer bit codecs and single-pass sources", "[integer_coding][views]")
{
  std::vector<std::int64_t> const values{ -5, -1, 0, 1, 5, std::numeric_limits<std::int64_t>::max() };
  REQUIRE(collect(values | pv::encode::zigzag() | pv::encode::positive() | pv::encode::gamma()
                  | pv::decode::gamma(values.size()) | pv::decode::positive() | pv::decode::zigzag())
          == values);
  REQUIRE(collect(values | pv::encode::zigzag() | pv::encode::positive() | pv::encode::delta()
                  | pv::decode::delta(values.size()) | pv::decode::positive() | pv::decode::zigzag())
          == values);
  std::vector<std::int64_t> const extremes{
    std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), 0
  };
  constexpr unsigned wide_shift = 63;
  REQUIRE(collect(extremes | pv::encode::zigzag() | pv::encode::rice(wide_shift)
                  | pv::decode::rice(wide_shift, extremes.size()) | pv::decode::zigzag())
          == extremes);
  std::istringstream source{ "-2 -1 0 1 2" };
  REQUIRE(collect(std::ranges::istream_view<int>(source) | pv::encode::zigzag() | pv::encode::golomb(golomb_divisor)
                  | pv::decode::golomb(golomb_divisor, signed_mapping_values.size()) | pv::decode::zigzag())
          == std::vector<std::int64_t>{ -2, -1, 0, 1, 2 });
}

TEST_CASE("integer mapping views defer work and domain errors until dereference", "[integer_coding][views]")
{
  std::size_t reads = 0;
  auto source = signed_mapping_values | std::views::transform([&](int value) {
    ++reads;
    return value;
  });
  auto mapped = source | pv::encode::zigzag();
  auto cursor = mapped.begin();
  REQUIRE(reads == 0);
  REQUIRE(*cursor == 0U);
  REQUIRE(reads == 1);
  REQUIRE(collect(mapped | std::views::take(0)).empty());
  REQUIRE(reads == 1);
  REQUIRE(collect(std::vector<int>{} | pv::encode::zigzag()).empty());

  std::array const maximum{ maximum_integer };
  auto overflow = maximum | pv::encode::positive();
  auto overflow_cursor = overflow.begin();
  REQUIRE(collect(overflow | std::views::take(0)).empty());
  REQUIRE_THROWS_MATCHES(
    *overflow_cursor, dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::numeric_overflow));
  std::array const zero{ std::uint64_t{ 0 } };
  auto invalid = zero | pv::decode::positive();
  REQUIRE_THROWS_MATCHES(
    *invalid.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  std::array const minimum{ std::numeric_limits<std::int64_t>::min() };
  auto signed_overflow = minimum | pv::encode::zigzag() | pv::encode::positive();
  REQUIRE_THROWS_MATCHES(
    *signed_overflow.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::numeric_overflow));
}

TEST_CASE("integer encoder views emit exact codewords", "[integer_coding][views]")
{
  std::vector<std::uint64_t> const positive{ 1, 2, 3, 4, 13 };
  REQUIRE(bit_string(positive | dv::encode::gamma()) == "1010011001000001101");
  REQUIRE(bit_string(positive | dv::encode::delta()) == "1010001010110000100101");
  std::vector<std::uint64_t> const nonnegative{ 0, 1, 4, 5 };
  REQUIRE(bit_string(nonnegative | dv::encode::golomb(golomb_divisor)) == "00000101111000");
  REQUIRE(bit_string(nonnegative | dv::encode::rice(rice_shift)) == "00000110001001");
}

TEST_CASE("all integer views round trip boundary values without packed storage", "[integer_coding][views]")
{
  std::vector<std::uint64_t> const positive{ 1, 2, 3, 4, 13, maximum_integer };
  REQUIRE(collect(positive | dv::encode::gamma() | dv::decode::gamma(positive.size())) == positive);
  REQUIRE(collect(positive | dv::encode::delta() | dv::decode::delta(positive.size())) == positive);
  std::vector<std::uint64_t> const nonnegative{ 0, 1, 4, 5, 13 };
  REQUIRE(
    collect(nonnegative | dv::encode::golomb(golomb_divisor) | dv::decode::golomb(golomb_divisor, nonnegative.size()))
    == nonnegative);
  REQUIRE(collect(nonnegative | dv::encode::rice(rice_shift) | dv::decode::rice(rice_shift, nonnegative.size()))
          == nonnegative);
  constexpr unsigned wide_shift = dacoexec::detail::integer_coding::integer_bits - 1U;
  std::array const extreme{ maximum_integer };
  REQUIRE(collect(extreme | dv::encode::rice(wide_shift) | dv::decode::rice(wide_shift, extreme.size()))
          == std::vector<std::uint64_t>{ maximum_integer });
}

TEST_CASE("integer views agree with the low-level writers and compose with packed bits", "[integer_coding][views]")
{
  std::vector<std::uint64_t> const values{ 1, 2, 3, 4, 13 };
  auto verify = [&]<class Encode, class Decode, class Write>(Encode encode, Decode decode, Write write) -> void {
    std::vector<std::byte> expected;
    dacoexec::bit_writer reference{ std::back_inserter(expected) };
    for (auto const value : values) { write(reference, value); }
    auto const expected_padding = reference.finish();
    std::vector<std::byte> packed;
    dacoexec::bit_writer writer{ std::back_inserter(packed) };
    for (bool const bit : values | encode) { writer.write_bit(bit); }
    REQUIRE(writer.finish() == expected_padding);
    REQUIRE(packed == expected);
    REQUIRE(collect(packed | dv::bits() | decode) == values);
  };
  verify(dv::encode::gamma(), dv::decode::gamma(values.size()), [](auto &writer, std::uint64_t value) -> void {
    dacoexec::detail::integer_coding::write_gamma(writer, value);
  });
  verify(dv::encode::delta(), dv::decode::delta(values.size()), [](auto &writer, std::uint64_t value) -> void {
    dacoexec::detail::integer_coding::write_delta(writer, value);
  });
  verify(dv::encode::golomb(golomb_divisor),
    dv::decode::golomb(golomb_divisor, values.size()),
    [](auto &writer, std::uint64_t value) -> void {
      dacoexec::detail::integer_coding::write_golomb(writer, value, golomb_divisor);
    });
  verify(dv::encode::rice(rice_shift),
    dv::decode::rice(rice_shift, values.size()),
    [](auto &writer, std::uint64_t value) -> void {
      dacoexec::detail::integer_coding::write_rice(writer, value, rice_shift);
    });
}

TEST_CASE("integer views stage lazily and preserve shared cursors across moves", "[integer_coding][views]")
{
  std::size_t reads = 0;
  std::vector<std::uint64_t> const values{ 1, 2 };
  auto source = values | std::views::transform([&](std::uint64_t value) -> std::uint64_t {
    ++reads;
    return value;
  });
  auto encoded = source | dv::encode::gamma();
  REQUIRE(reads == 0);
  auto cursor = encoded.begin();
  auto alias = encoded.begin();
  REQUIRE(reads == 1);
  REQUIRE(*cursor);
  auto moved = std::move(encoded);
  ++cursor;
  REQUIRE_FALSE(*alias);
  REQUIRE(reads == values.size());
  REQUIRE_FALSE(*moved.begin());

  auto decoded = values | dv::encode::gamma() | dv::decode::gamma(values.size());
  auto position = decoded.begin();
  auto copy = decoded.begin();
  auto owner = std::move(decoded);
  REQUIRE(*position == values.front());
  ++position;
  REQUIRE(*copy == values.back());
  REQUIRE(*owner.begin() == values.back());
  ++position;
  REQUIRE(position == owner.end());
}

TEST_CASE("integer decoders honor counts and leave trailing bits unread", "[integer_coding][views]")
{
  std::size_t reads = 0;
  std::vector<bool> const bits{ true, false, true };
  auto source = bits | std::views::transform([&](bool bit) -> bool {
    ++reads;
    return bit;
  });
  auto empty = source | dv::decode::gamma(0);
  REQUIRE(empty.begin() == empty.end());
  REQUIRE(reads == 0);
  auto single = source | dv::decode::gamma(1);
  REQUIRE(collect(single) == std::vector<std::uint64_t>{ 1 });
  REQUIRE(reads == 1);
}

TEST_CASE("integer decoding stops without staging the next codeword", "[integer_coding][views]")
{
  std::vector<std::uint64_t> const values{ 1, 0 };
  auto gamma = values | dv::encode::gamma() | dv::decode::gamma(1);
  REQUIRE(collect(gamma) == std::vector<std::uint64_t>{ 1 });
  auto delta = values | dv::encode::delta() | dv::decode::delta(1);
  REQUIRE(collect(delta) == std::vector<std::uint64_t>{ 1 });
}

TEST_CASE("integer decoding defers source increments until another bit is needed", "[integer_coding][views]")
{
  std::size_t predicate_calls = 0;
  std::vector<bool> const bits{ true, false };
  // Incrementing the filter cursor searches for the next accepted element.
  // That search must not happen after the last requested integer.
  auto source = bits | std::views::filter([&](bool bit) -> bool {
    ++predicate_calls;
    if (!bit) { throw std::runtime_error("source increment failure"); }
    return true;
  });
  auto single = source | dv::decode::gamma(1);
  REQUIRE(collect(single) == std::vector<std::uint64_t>{ 1 });
  REQUIRE(predicate_calls == 1);

  auto multiple = source | dv::decode::gamma(bits.size());
  auto cursor = multiple.begin();
  REQUIRE(*cursor == 1);
  REQUIRE_THROWS_AS(++cursor, std::runtime_error);
  REQUIRE(cursor == multiple.end());
  auto const calls_after_failure = predicate_calls;
  REQUIRE(multiple.begin() == multiple.end());
  REQUIRE(predicate_calls == calls_after_failure);
}

TEST_CASE("integer views reject invalid parameters and terminate after malformed input", "[integer_coding][views]")
{
  constexpr auto invalid_shift = dacoexec::detail::integer_coding::integer_bits;
  REQUIRE_THROWS_MATCHES(
    dv::encode::golomb(0), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE_THROWS_MATCHES(
    dv::decode::golomb(0, 0), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE_THROWS_MATCHES(
    dv::encode::rice(invalid_shift), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE_THROWS_MATCHES(dv::decode::rice(invalid_shift, 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  std::vector<std::uint64_t> const zero{ 0 };
  auto gamma = zero | dv::encode::gamma();
  REQUIRE_THROWS_MATCHES(
    gamma.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE(gamma.begin() == gamma.end());
  auto delta = zero | dv::encode::delta();
  REQUIRE_THROWS_MATCHES(
    delta.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE(delta.begin() == delta.end());
  std::vector<bool> const truncated{ false, true };
  auto decoder = truncated | dv::decode::gamma(1);
  REQUIRE_THROWS_MATCHES(
    decoder.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::truncated_input));
  REQUIRE(decoder.begin() == decoder.end());
  std::vector<bool> const overflow(dacoexec::detail::integer_coding::integer_bits, false);
  auto oversized = overflow | dv::decode::gamma(1);
  REQUIRE_THROWS_MATCHES(
    oversized.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::numeric_overflow));
  REQUIRE(oversized.begin() == oversized.end());
  constexpr dacoexec::detail::integer_coding::decode_limits limits{ .max_unary_bits_ = 1 };
  std::vector<bool> const unary{ true, true, false };
  auto rice = unary | dv::decode::rice(0, 1, limits);
  REQUIRE_THROWS_MATCHES(
    rice.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::codeword_limit_exceeded));
  REQUIRE(rice.begin() == rice.end());
  auto golomb = unary | dv::decode::golomb(1, 1, limits);
  REQUIRE_THROWS_MATCHES(
    golomb.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::codeword_limit_exceeded));
}

TEST_CASE("unbounded unary codewords start without materializing their prefix", "[integer_coding][views]")
{
  std::array const values{ maximum_integer };
  auto rice = values | dv::encode::rice(0);
  REQUIRE(*rice.begin());
  auto golomb = values | dv::encode::golomb(1);
  REQUIRE(*golomb.begin());
}

TEST_CASE("source failures permanently end integer views", "[integer_coding][views]")
{
  std::vector<std::uint64_t> const values{ 1 };
  auto integers = values | std::views::transform([](std::uint64_t) -> std::uint64_t {
    throw std::runtime_error("integer source failure");
  });
  auto encoded = integers | dv::encode::gamma();
  REQUIRE_THROWS_AS(encoded.begin(), std::runtime_error);
  REQUIRE(encoded.begin() == encoded.end());
  std::vector<bool> const bits{ true };
  auto input = bits | std::views::transform([](bool) -> bool { throw std::runtime_error("bit source failure"); });
  auto decoded = input | dv::decode::delta(1);
  REQUIRE_THROWS_AS(decoded.begin(), std::runtime_error);
  REQUIRE(decoded.begin() == decoded.end());
}
