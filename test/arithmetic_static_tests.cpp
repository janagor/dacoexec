#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/detail/arithmetic/range/static/codec.hpp>

#include <array>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <random>
#include <ranges>
#include <sstream>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
namespace model = dacoexec::detail::model;

struct noncopyable_byte
{
  std::byte value_;

  explicit noncopyable_byte(std::byte value) : value_(value) {}

  noncopyable_byte(noncopyable_byte const &) = delete;
  auto operator=(noncopyable_byte const &) -> noncopyable_byte & = delete;

  operator std::byte() const { return value_; }
};

struct proxy_range
{
  struct iterator
  {
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;
    noncopyable_byte *position_ = nullptr;

    auto operator*() const -> noncopyable_byte & { return *position_; }

    auto operator++() -> iterator &
    {
      ++position_;
      return *this;
    }

    void operator++(int) { ++*this; }

    auto operator==(iterator const &) const -> bool = default;
  };

  std::span<noncopyable_byte> values_;

  auto begin() const -> iterator { return { values_.data() }; }

  auto end() const -> iterator { return { values_.data() + values_.size() }; }
};

static_assert(dacoexec::detail::byte_input_range<proxy_range>);
static_assert(!std::ranges::forward_range<proxy_range>);
static_assert(!std::copy_constructible<noncopyable_byte>);

auto bytes(std::initializer_list<unsigned> values) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  for (auto value : values) { result.push_back(static_cast<std::byte>(value)); }
  return result;
}

void check_round_trip(std::vector<std::byte> const &input, unsigned precision = 12)
{
  auto const probabilities = arithmetic::make_static_model(input, precision);
  auto const metadata = arithmetic::serialize_model(probabilities);
  auto const restored = arithmetic::deserialize_model(metadata);
  REQUIRE(restored.frequencies() == probabilities.frequencies());
  REQUIRE(restored.cumulative() == probabilities.cumulative());
  auto const payload = arithmetic::encode_block(input, probabilities);
  REQUIRE(arithmetic::encode_block(input, probabilities) == payload);
  REQUIRE(arithmetic::decode_block(payload, restored, input.size(), input.size()) == input);
}
}// namespace

TEST_CASE("Static range model reuses shared normalization", "[arithmetic][model]")
{
  auto const input = bytes({ 'A', 'B', 'A', 'A', 'C', 'B' });
  auto const probabilities = arithmetic::make_static_model(input);
  REQUIRE(probabilities.precision_bits() == 12);
  REQUIRE(probabilities.total() == 4096);
  REQUIRE(probabilities.frequencies() == model::make_model(model::count_frequencies(input)).frequencies());
  REQUIRE(probabilities.frequencies().at('A') == 2048);
  REQUIRE(probabilities.frequencies().at('B') == 1365);
  REQUIRE(probabilities.frequencies().at('C') == 683);
  std::uint32_t sum = 0;
  for (std::size_t symbol = 0; symbol < model::alphabet_size; ++symbol) {
    auto const frequency = probabilities.frequencies().at(symbol);
    REQUIRE(probabilities.cumulative().at(symbol) == sum);
    for (std::uint32_t offset = 0; offset < frequency; ++offset) {
      REQUIRE(arithmetic::find_symbol(probabilities, sum + offset) == symbol);
    }
    sum += frequency;
  }
  REQUIRE(sum == 4096);
  REQUIRE_THROWS_MATCHES(arithmetic::find_symbol(probabilities, 4096),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
}

TEST_CASE("Static range blocks cover sparse skewed and full byte alphabets", "[arithmetic][model]")
{
  check_round_trip(bytes({ 0 }));
  check_round_trip(std::vector<std::byte>(100000, std::byte{ 255 }));
  std::vector<std::byte> all;
  for (unsigned index = 0; index < 65536; ++index) { all.push_back(static_cast<std::byte>(index % 256)); }
  check_round_trip(all);
  std::mt19937 random(42);
  for (unsigned alphabet : { 2U, 17U, 256U }) {
    std::vector<std::byte> input(20000);
    for (auto &value : input) { value = static_cast<std::byte>(random() % alphabet); }
    for (unsigned precision : { 8U, 12U, 16U }) { check_round_trip(input, precision); }
  }
  std::vector<std::byte> skewed(100000, std::byte{ 0 });
  for (unsigned symbol = 1; symbol < 256; ++symbol) { skewed.push_back(static_cast<std::byte>(symbol)); }
  auto const probabilities = arithmetic::make_static_model(skewed);
  for (auto frequency : probabilities.frequencies()) { REQUIRE(frequency > 0); }
  check_round_trip(skewed);
}

TEST_CASE("Static range adapter accepts single pass and signed byte sources", "[arithmetic][model]")
{
  std::vector<signed char> const input{ 0, -128, -1, 127, -128 };
  auto const probabilities = arithmetic::make_static_model(input);
  auto const payload = arithmetic::encode_block(input, probabilities);
  REQUIRE(
    arithmetic::decode_block(payload, probabilities, input.size(), input.size()) == bytes({ 0, 128, 255, 127, 128 }));
  std::istringstream counting("ABAACBBB");
  std::istringstream encoding("ABAACBBB");
  auto const single_pass = arithmetic::make_static_model(std::ranges::istream_view<char>(counting));
  auto const encoded = arithmetic::encode_block(std::ranges::istream_view<char>(encoding), single_pass);
  REQUIRE(arithmetic::decode_block(encoded, single_pass, 8, 8) == bytes({ 'A', 'B', 'A', 'A', 'C', 'B', 'B', 'B' }));
}

TEST_CASE("Static range metadata has deterministic sparse representation", "[arithmetic][model]")
{
  auto const probabilities = arithmetic::make_static_model(bytes({ 0, 255 }));
  auto const golden = bytes({ 12, 2, 0, 0, 255, 7, 255, 255, 7 });
  REQUIRE(arithmetic::serialize_model(probabilities) == golden);
  REQUIRE(arithmetic::serialize_model(arithmetic::deserialize_model(golden)) == golden);
  auto const single = arithmetic::make_static_model(bytes({ 255 }), 16);
  REQUIRE(arithmetic::serialize_model(single) == bytes({ 16, 1, 0, 255, 255, 255 }));
  REQUIRE(arithmetic::deserialize_model(arithmetic::serialize_model(single)).frequencies() == single.frequencies());
}

TEST_CASE("Static range encoding accepts noncopyable lvalue proxies", "[arithmetic][model]")
{
  std::array values{ noncopyable_byte{ std::byte{ 0 } },
    noncopyable_byte{ std::byte{ 128 } },
    noncopyable_byte{ std::byte{ 255 } },
    noncopyable_byte{ std::byte{ 128 } } };
  proxy_range source{ values };
  auto const probabilities = arithmetic::make_static_model(source);
  auto const payload = arithmetic::encode_block(source, probabilities);
  REQUIRE(
    arithmetic::decode_block(payload, probabilities, values.size(), values.size()) == bytes({ 0, 128, 255, 128 }));
}

TEST_CASE("Static range models round trip at every supported precision", "[arithmetic][model]")
{
  for (unsigned precision = 1; precision <= 16; ++precision) {
    CAPTURE(precision);
    check_round_trip(bytes({ 0, 255, 0, 0, 255, 0 }), precision);
    if (precision >= 2) { check_round_trip(bytes({ 0, 128, 255, 127, 255, 128, 0 }), precision); }
    auto const alphabet = std::min(256U, 1U << precision);
    std::vector<std::byte> input;
    for (unsigned symbol = 0; symbol < alphabet; ++symbol) { input.push_back(static_cast<std::byte>(symbol)); }
    // Both dense and sparse models exercise all slots, including long gaps.
    for (auto const &symbols : { input, bytes({ 0, 128, 255 }) }) {
      if (precision == 1 && symbols.size() > alphabet) { continue; }
      auto const probabilities = arithmetic::make_static_model(symbols, precision);
      auto const lookup = arithmetic::make_symbol_lookup(probabilities);
      REQUIRE(lookup.size() == (1U << precision));
      for (std::uint32_t slot = 0; slot < probabilities.total(); ++slot) {
        REQUIRE(lookup.at(slot) == arithmetic::find_symbol(probabilities, slot));
      }
    }
  }
}

TEST_CASE("Static range block integration preserves golden bytes", "[arithmetic][model]")
{
  auto const input = bytes({ 0, 255, 255, 0, 255, 0, 0, 255 });
  auto const probabilities = arithmetic::make_static_model(input, 1);
  auto const golden = bytes({ 0, 0x68, 0xff, 0xff, 0xfc, 0 });
  REQUIRE(arithmetic::serialize_model(probabilities) == bytes({ 1, 2, 0, 0, 0, 0, 255, 0, 0 }));
  REQUIRE(arithmetic::encode_block(input, probabilities) == golden);
  REQUIRE(arithmetic::decode_block(golden, probabilities, input.size(), input.size()) == input);
}

TEST_CASE("Static range model rejects malformed metadata and invalid sources", "[arithmetic][model]")
{
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_payload);
  auto const valid = bytes({ 12, 2, 0, 0, 255, 7, 255, 255, 7 });
  for (std::size_t size = 0; size < valid.size(); ++size) {
    REQUIRE_THROWS_MATCHES(arithmetic::deserialize_model(std::span(valid).first(size)), dacoexec::exception, invalid);
  }
  for (auto const &bad : { bytes({ 0, 1, 0, 0, 0, 0 }),
         bytes({ 17, 1, 0, 0, 0, 0 }),
         bytes({ 12, 0, 0 }),
         bytes({ 12, 1, 1 }),
         bytes({ 12, 1, 0, 0, 0, 16 }),
         bytes({ 12, 1, 0, 0, 0, 0 }),
         bytes({ 12, 2, 0, 0, 255, 7, 0, 255, 7 }),
         bytes({ 12, 2, 0, 255, 255, 7, 0, 255, 7 }),
         bytes({ 12, 1, 0, 0, 255, 15, 0 }) }) {
    REQUIRE_THROWS_MATCHES(arithmetic::deserialize_model(bad), dacoexec::exception, invalid);
  }
  auto const argument = dacoexec::test::error_is(dacoexec::errc::invalid_argument);
  REQUIRE_THROWS_MATCHES(arithmetic::make_static_model(std::vector<std::byte>{}), dacoexec::exception, argument);
  REQUIRE_THROWS_MATCHES(arithmetic::make_static_model(bytes({ 1 }), 0), dacoexec::exception, argument);
  REQUIRE_THROWS_MATCHES(arithmetic::make_static_model(bytes({ 1 }), 17), dacoexec::exception, argument);
  REQUIRE_THROWS_MATCHES(arithmetic::make_static_model(bytes({ 0, 1, 2 }), 1), dacoexec::exception, argument);
  auto const probabilities = arithmetic::make_static_model(bytes({ 0 }));
  REQUIRE_THROWS_MATCHES(arithmetic::encode_block(bytes({ 1 }), probabilities), dacoexec::exception, argument);
}

TEST_CASE("Static range adapter checks payload termination and output limits", "[arithmetic][model]")
{
  auto const input = bytes({ 0, 255, 128, 0, 128, 255, 255 });
  auto const probabilities = arithmetic::make_static_model(input);
  auto const payload = arithmetic::encode_block(input, probabilities);
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_payload);
  for (std::size_t size = 0; size < payload.size(); ++size) {
    REQUIRE_THROWS_MATCHES(
      arithmetic::decode_block(std::span(payload).first(size), probabilities, input.size(), input.size()),
      dacoexec::exception,
      invalid);
  }
  auto trailing = payload;
  trailing.push_back(std::byte{ 0 });
  REQUIRE_THROWS_MATCHES(
    arithmetic::decode_block(trailing, probabilities, input.size(), input.size()), dacoexec::exception, invalid);
  REQUIRE_THROWS_MATCHES(arithmetic::decode_block({}, probabilities, input.size(), input.size() - 1),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  REQUIRE_THROWS_MATCHES(
    arithmetic::decode_block(payload, probabilities, std::numeric_limits<std::size_t>::max(), 1024),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  auto const empty = arithmetic::encode_block(std::vector<std::byte>{}, probabilities);
  REQUIRE(empty == bytes({ 0, 0, 0, 0, 0 }));
  REQUIRE(arithmetic::decode_block(empty, probabilities, 0, 0).empty());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
