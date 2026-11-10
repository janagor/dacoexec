#include "error_test_utils.hpp"
#include <dacoexec/bit.hpp>
#include <dacoexec/detail/integer_coding/common.hpp>
#include <dacoexec/detail/integer_coding/delta.hpp>
#include <dacoexec/detail/integer_coding/golomb.hpp>
#include <dacoexec/detail/integer_coding/rice.hpp>
#include <dacoexec/detail/integer_coding/sequence.hpp>
#include <dacoexec/detail/numeric/mapping.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

constexpr auto signed_minimum = std::numeric_limits<std::int64_t>::min();
constexpr auto signed_maximum = std::numeric_limits<std::int64_t>::max();
constexpr auto unsigned_maximum = std::numeric_limits<std::uint64_t>::max();
constexpr auto byte_bits = dacoexec::detail::support::bits_per_byte;
constexpr auto rice_shift = 2U;
constexpr auto golomb_divisor = 5U;
constexpr auto sequence_repetitions = 64U;
constexpr std::array<std::uint64_t, 4> positive_values{ 1U, 2U, 3U, 13U };

struct single_pass_numbers
{
  std::unique_ptr<std::istringstream> stream_;

  [[nodiscard]] auto begin() const -> std::istream_iterator<std::uint64_t>
  { return std::istream_iterator<std::uint64_t>{ *stream_ }; }

  static auto end() -> std::istream_iterator<std::uint64_t> { return {}; }
};

static_assert(dacoexec::detail::integer_coding::unsigned_integer_range<single_pass_numbers>);
static_assert(!std::ranges::forward_range<single_pass_numbers>);
static_assert(!std::copy_constructible<single_pass_numbers>);
static_assert(!dacoexec::detail::integer_coding::unsigned_integer_range<std::array<std::int64_t, 1>>);
static_assert(!dacoexec::detail::integer_coding::unsigned_integer_range<std::array<bool, 1>>);
static_assert(dacoexec::detail::numeric::zigzag_encode(signed_minimum) == unsigned_maximum);
static_assert(dacoexec::detail::numeric::zigzag_decode(unsigned_maximum) == signed_minimum);
static_assert(
  dacoexec::detail::numeric::zigzag_decode(dacoexec::detail::numeric::zigzag_encode(signed_maximum)) == signed_maximum);
static_assert(dacoexec::detail::numeric::to_positive(0U) == 1U);
static_assert(dacoexec::detail::numeric::from_positive(unsigned_maximum) == unsigned_maximum - 1U);

constexpr auto constexpr_sequence() -> bool
{
  constexpr auto gamma_bits = 7U;
  std::array<std::byte, 1> const bytes{ std::byte{ 0b10100110 } };
  std::array<std::uint64_t, 3> values{};
  dacoexec::bit_reader reader{ bytes, gamma_bits };
  auto *const end = dacoexec::detail::integer_coding::read_gamma_sequence(reader, values.size(), values.begin());
  return end == values.end() && values == std::array<std::uint64_t, 3>{ 1U, 2U, 3U } && reader.remaining_bits() == 0U;
}

static_assert(constexpr_sequence());

}// namespace

TEST_CASE("integer coding sequence helpers concatenate codecs without finalizing", "[integer_coding][sequence]")
{
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  for (unsigned index = 0; index < sequence_repetitions; ++index) {
    dacoexec::detail::integer_coding::write_gamma_sequence(writer, positive_values);
    dacoexec::detail::integer_coding::write_delta_sequence(writer, positive_values);
    dacoexec::detail::integer_coding::write_golomb_sequence(writer, positive_values, golomb_divisor);
    dacoexec::detail::integer_coding::write_rice_sequence(writer, positive_values, rice_shift);
  }
  auto const padding = writer.finish();
  dacoexec::bit_reader reader{ bytes, (bytes.size() * byte_bits) - padding };
  for (unsigned index = 0; index < sequence_repetitions; ++index) {
    std::array<std::uint64_t, positive_values.size()> decoded{};
    REQUIRE(
      dacoexec::detail::integer_coding::read_gamma_sequence(reader, decoded.size(), decoded.begin()) == decoded.end());
    REQUIRE(decoded == positive_values);
    dacoexec::detail::integer_coding::read_delta_sequence(reader, decoded.size(), decoded.begin());
    REQUIRE(decoded == positive_values);
    dacoexec::detail::integer_coding::read_golomb_sequence(reader, decoded.size(), decoded.begin(), golomb_divisor);
    REQUIRE(decoded == positive_values);
    dacoexec::detail::integer_coding::read_rice_sequence(reader, decoded.size(), decoded.begin(), rice_shift);
    REQUIRE(decoded == positive_values);
  }
  REQUIRE(reader.remaining_bits() == 0U);
}

TEST_CASE("integer coding sequences accept move-only single-pass sources", "[integer_coding][sequence]")
{
  single_pass_numbers const source{ std::make_unique<std::istringstream>("1 2 3 13") };
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  dacoexec::detail::integer_coding::write_gamma_sequence(writer, source);
  auto const padding = writer.finish();
  dacoexec::bit_reader reader{ bytes, (bytes.size() * byte_bits) - padding };
  std::vector<std::uint64_t> decoded;
  dacoexec::detail::integer_coding::read_gamma_sequence(reader, positive_values.size(), std::back_inserter(decoded));
  REQUIRE(std::ranges::equal(decoded, positive_values));
}

TEST_CASE("signed mappings compose with integer coding sequences", "[integer_coding][sequence]")
{
  constexpr std::array<std::int64_t, 5> signed_values{ 0, -1, 1, -2, signed_maximum };
  for (auto const value : signed_values) {
    REQUIRE(dacoexec::detail::numeric::zigzag_decode(dacoexec::detail::numeric::zigzag_encode(value)) == value);
  }
  REQUIRE_THROWS_AS(dacoexec::detail::numeric::to_positive(dacoexec::detail::numeric::zigzag_encode(signed_minimum)),
    dacoexec::exception);
  REQUIRE_THROWS_AS(dacoexec::detail::numeric::from_positive(0U), dacoexec::exception);

  auto mapped = signed_values | std::views::transform([](std::int64_t value) -> std::uint64_t {
    return dacoexec::detail::numeric::to_positive(dacoexec::detail::numeric::zigzag_encode(value));
  });
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  dacoexec::detail::integer_coding::write_delta_sequence(writer, mapped);
  auto const padding = writer.finish();
  dacoexec::bit_reader reader{ bytes, (bytes.size() * byte_bits) - padding };
  std::vector<std::uint64_t> decoded;
  dacoexec::detail::integer_coding::read_sequence(
    reader, signed_values.size(), std::back_inserter(decoded), [](dacoexec::bit_reader &input_reader) -> std::uint64_t {
      return dacoexec::detail::numeric::from_positive(dacoexec::detail::integer_coding::read_delta(input_reader));
    });
  for (std::size_t index = 0; index < signed_values.size(); ++index) {
    REQUIRE(dacoexec::detail::numeric::zigzag_decode(decoded.at(index)) == signed_values.at(index));
  }
}

TEST_CASE("empty sequences and failures preserve explicit boundaries", "[integer_coding][sequence]")
{
  std::array<std::uint64_t, 0> const empty{};
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  dacoexec::detail::integer_coding::write_gamma_sequence(writer, empty);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::write_rice_sequence(
                      writer, empty, dacoexec::detail::integer_coding::integer_bits),
    dacoexec::exception);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::write_golomb_sequence(writer, empty, 0U), dacoexec::exception);
  dacoexec::detail::integer_coding::write_gamma_sequence(writer, positive_values);
  auto const padding = writer.finish();
  dacoexec::bit_reader reader{ bytes, (bytes.size() * byte_bits) - padding };
  std::vector<std::uint64_t> decoded;
  dacoexec::detail::integer_coding::read_gamma_sequence(reader, 0U, std::back_inserter(decoded));
  REQUIRE(decoded.empty());
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_golomb_sequence(reader, 0U, std::back_inserter(decoded), 0U),
    dacoexec::exception);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_rice_sequence(
                      reader, 0U, std::back_inserter(decoded), dacoexec::detail::integer_coding::integer_bits),
    dacoexec::exception);
  REQUIRE(reader.remaining_bits() == (bytes.size() * byte_bits) - padding);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_gamma_sequence(
                      reader, positive_values.size() + 1U, std::back_inserter(decoded)),
    dacoexec::exception);
  REQUIRE(std::ranges::equal(decoded, positive_values));
}

TEST_CASE("sequence callbacks and decoding limits propagate failures", "[integer_coding][sequence]")
{
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  auto const fail_on_second = [](auto &out, std::uint64_t value) -> void {
    if (value != positive_values.front()) { throw std::runtime_error("sequence encoder failure"); }
    dacoexec::detail::integer_coding::write_gamma(out, value);
  };
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::write_sequence(writer, positive_values, fail_on_second), std::runtime_error);
  auto const padding = writer.finish();
  dacoexec::bit_reader reader{ bytes, (bytes.size() * byte_bits) - padding };
  std::vector<std::uint64_t> decoded;
  dacoexec::detail::integer_coding::read_gamma_sequence(reader, 1U, std::back_inserter(decoded));
  REQUIRE(decoded == std::vector<std::uint64_t>{ positive_values.front() });
  REQUIRE(reader.remaining_bits() == 0U);

  bytes.clear();
  dacoexec::bit_writer rice_writer{ std::back_inserter(bytes) };
  constexpr std::array<std::uint64_t, 2> quotients{ 0U, 1U };
  dacoexec::detail::integer_coding::write_rice_sequence(rice_writer, quotients, 0U);
  auto const rice_padding = rice_writer.finish();
  for (bool const golomb : std::array{ false, true }) {
    dacoexec::bit_reader bounded{ bytes, (bytes.size() * byte_bits) - rice_padding };
    decoded.clear();
    auto const decode = [&](dacoexec::bit_reader &input_reader) -> std::uint64_t {
      constexpr dacoexec::detail::integer_coding::decode_limits limits{ .max_unary_bits_ = 0U };
      return golomb ? dacoexec::detail::integer_coding::read_golomb(input_reader, 1U, limits)
                    : dacoexec::detail::integer_coding::read_rice(input_reader, 0U, limits);
    };
    REQUIRE_THROWS_MATCHES(
      dacoexec::detail::integer_coding::read_sequence(bounded, quotients.size(), std::back_inserter(decoded), decode),
      dacoexec::exception,
      dacoexec::test::error_is(dacoexec::errc::codeword_limit_exceeded));
    REQUIRE(decoded == std::vector<std::uint64_t>{ 0U });
  }
}
