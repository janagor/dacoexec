#include "error_test_utils.hpp"
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/detail/arithmetic/range/adaptive/fenwick_model.hpp>
#include <dacoexec/detail/arithmetic/range/adaptive/model.hpp>
#include <dacoexec/detail/arithmetic/range/decoder.hpp>
#include <dacoexec/detail/arithmetic/range/encoder.hpp>

#include <array>
#include <numeric>
#include <random>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace arithmetic = dacoexec::detail::arithmetic;

template<class Model> void check_intervals(Model const &probabilities)
{
  std::uint32_t sum = 0;
  for (std::size_t symbol = 0; symbol < 256; ++symbol) {
    auto const frequency = probabilities.frequencies().at(symbol);
    REQUIRE(frequency >= 1);
    auto const interval = probabilities.interval_for(static_cast<std::uint8_t>(symbol));
    REQUIRE(interval.cumulative_ == sum);
    REQUIRE(interval.frequency_ == frequency);
    REQUIRE(interval.total_ == probabilities.total());
    REQUIRE(arithmetic::find_symbol(probabilities, sum) == symbol);
    REQUIRE(arithmetic::find_symbol(probabilities, sum + frequency - 1) == symbol);
    sum += frequency;
  }
  REQUIRE(probabilities.total() == sum);
  REQUIRE(sum <= arithmetic::adaptive_model::rescale_threshold);
}

template<class Model = arithmetic::adaptive_model>
auto encode(std::vector<std::uint8_t> const &input) -> std::vector<std::byte>
{
  Model probabilities;
  arithmetic::range_encoder encoder;
  for (auto symbol : input) {
    auto const interval = probabilities.interval_for(symbol);
    encoder.encode(interval.cumulative_, interval.frequency_, interval.total_);
    probabilities.update(symbol);
  }
  return encoder.finish();
}
}// namespace

TEMPLATE_TEST_CASE("Adaptive byte model starts uniform and updates after each symbol",
  "[arithmetic][model][adaptive]",
  arithmetic::adaptive_model,
  arithmetic::fenwick_adaptive_model)
{
  TestType probabilities;
  REQUIRE(probabilities.total() == 256);
  check_intervals(probabilities);
  for (auto symbol : { std::uint8_t{ 0 }, std::uint8_t{ 255 }, std::uint8_t{ 128 }, std::uint8_t{ 128 } }) {
    auto const previous = probabilities.frequencies();
    probabilities.update(symbol);
    for (std::size_t index = 0; index < 256; ++index) {
      REQUIRE(probabilities.frequencies().at(index) == previous.at(index) + (index == symbol ? 1U : 0U));
    }
    check_intervals(probabilities);
  }
  REQUIRE(probabilities.total() == 260);
  REQUIRE_THROWS_MATCHES(arithmetic::find_symbol(probabilities, probabilities.total()),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
}

TEMPLATE_TEST_CASE("Adaptive rescaling occurs strictly above the fixed threshold",
  "[arithmetic][model][adaptive]",
  arithmetic::adaptive_model,
  arithmetic::fenwick_adaptive_model)
{
  TestType probabilities;
  for (unsigned index = 256; index < 32768; ++index) { probabilities.update(255); }
  REQUIRE(probabilities.total() == 32768);
  REQUIRE(probabilities.frequencies().at(255) == 32513);
  check_intervals(probabilities);
  probabilities.update(255);
  REQUIRE(probabilities.frequencies().at(255) == 16257);
  REQUIRE(probabilities.total() == 16512);
  check_intervals(probabilities);
  // A previously unseen byte remains encodable even after rescaling.
  probabilities.update(0);
  REQUIRE(probabilities.frequencies().at(0) == 2);
  check_intervals(probabilities);
}

TEMPLATE_TEST_CASE("Adaptive rescaling halves odd and even frequencies upward",
  "[arithmetic][model][adaptive]",
  arithmetic::adaptive_model,
  arithmetic::fenwick_adaptive_model)
{
  TestType probabilities;
  probabilities.rescale();
  REQUIRE(probabilities.total() == 256);
  for (unsigned count = 0; count < 5; ++count) {
    for (unsigned symbol = count; symbol < 5; ++symbol) { probabilities.update(static_cast<std::uint8_t>(symbol)); }
  }
  probabilities.rescale();
  std::array<std::uint32_t, 6> const expected{ 1, 2, 2, 3, 3, 1 };
  for (std::size_t symbol = 0; symbol < expected.size(); ++symbol) {
    REQUIRE(probabilities.frequencies().at(symbol) == expected.at(symbol));
  }
  check_intervals(probabilities);
}

TEMPLATE_TEST_CASE("Adaptive model matches an independent frequency history across rescales",
  "[arithmetic][model][adaptive]",
  arithmetic::adaptive_model,
  arithmetic::fenwick_adaptive_model)
{
  TestType probabilities;
  arithmetic::adaptive_model::table_type expected;
  expected.fill(1);
  std::mt19937 random(42);
  unsigned rescales = 0;
  for (unsigned index = 0; index < 150000; ++index) {
    auto const symbol = static_cast<std::uint8_t>(index < 50000 ? 0 : random() % 256);
    ++expected.at(symbol);
    auto total = std::accumulate(expected.begin(), expected.end(), std::uint32_t{ 0 });
    if (total > 32768) {
      for (auto &frequency : expected) { frequency = frequency / 2 + frequency % 2; }
      total = std::accumulate(expected.begin(), expected.end(), std::uint32_t{ 0 });
      ++rescales;
    }
    probabilities.update(symbol);
    if (index % 4096 == 0 || index == 149999) {
      REQUIRE(probabilities.frequencies() == expected);
      REQUIRE(probabilities.total() == total);
      check_intervals(probabilities);
    }
  }
  REQUIRE(rescales >= 7);
}

TEMPLATE_TEST_CASE("Adaptive models synchronize through the existing range engine",
  "[arithmetic][model][adaptive]",
  arithmetic::adaptive_model,
  arithmetic::fenwick_adaptive_model)
{
  std::vector<std::uint8_t> input;
  std::mt19937 random(123);
  for (unsigned index = 0; index < 100000; ++index) {
    input.push_back(static_cast<std::uint8_t>(index < 40000 ? 255 : random() % 256));
  }
  auto const payload = encode<TestType>(input);
  REQUIRE(payload == encode<arithmetic::adaptive_model>(input));
  REQUIRE(payload == encode<arithmetic::fenwick_adaptive_model>(input));
  REQUIRE(encode<TestType>(input) == payload);
  TestType probabilities;
  arithmetic::range_decoder decoder(payload);
  for (auto expected : input) {
    auto const symbol = arithmetic::find_symbol(probabilities, decoder.cumulative(probabilities.total()));
    REQUIRE(symbol == expected);
    auto const interval = probabilities.interval_for(symbol);
    decoder.decode(interval.cumulative_, interval.frequency_, interval.total_);
    probabilities.update(symbol);
  }
  decoder.finish();
  REQUIRE(encode<TestType>({}) == std::vector<std::byte>(5, std::byte{ 0 }));
  TestType fresh;
  REQUIRE(fresh.total() == 256);
  check_intervals(fresh);
}

TEST_CASE("Adaptive representations agree on arbitrary slots and all prefixes", "[arithmetic][model][adaptive]")
{
  arithmetic::adaptive_model array;
  arithmetic::fenwick_adaptive_model tree;
  std::mt19937 random(1729);
  for (unsigned index = 0; index < 150000; ++index) {
    auto const symbol = static_cast<std::uint8_t>(index < 40000 ? 0 : random() % 256);
    array.update(symbol);
    tree.update(symbol);
    REQUIRE(tree.total() == array.total());
    auto const slot = static_cast<std::uint32_t>(random() % array.total());
    REQUIRE(tree.find_symbol(slot) == array.find_symbol(slot));
    auto const interval = tree.interval_for(symbol);
    REQUIRE(interval.cumulative_ == array.cumulative().at(symbol));
    REQUIRE(interval.frequency_ == array.frequencies().at(symbol));
    if (index == 0 || index == 32511 || index == 32512 || index % 16384 == 0 || index == 149999) {
      REQUIRE(tree.frequencies() == array.frequencies());
      check_intervals(tree);
      for (std::uint32_t cumulative = 0; cumulative < array.total(); ++cumulative) {
        REQUIRE(tree.find_symbol(cumulative) == array.find_symbol(cumulative));
      }
    }
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
