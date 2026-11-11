#include "error_test_utils.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/binary_decoder.hpp>
#include <dacoexec/detail/arithmetic/binary_encoder.hpp>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <random>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <vector>

// Explicit probabilities, golden bytes, and malformed inputs.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
using arithmetic::binary_model;

auto bytes(std::initializer_list<unsigned> values) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  for (auto value : values) { result.push_back(static_cast<std::byte>(value)); }
  return result;
}

auto encode(std::span<unsigned const> bits, binary_model probabilities) -> std::vector<std::byte>
{
  arithmetic::binary_arithmetic_encoder encoder;
  for (auto bit : bits) { encoder.encode(bit != 0, probabilities); }
  return encoder.finish();
}

void check(std::span<unsigned const> bits, binary_model probabilities, std::span<std::byte const> payload)
{
  arithmetic::binary_arithmetic_decoder decoder(payload);
  for (auto bit : bits) { REQUIRE(decoder.decode(probabilities) == (bit != 0)); }
  decoder.finish();
}

void consume(std::size_t bit_count, binary_model probabilities, std::span<std::byte const> payload)
{
  arithmetic::binary_arithmetic_decoder decoder(payload);
  for (std::size_t index = 0; index < bit_count; ++index) { static_cast<void>(decoder.decode(probabilities)); }
  decoder.finish();
}
}// namespace

TEST_CASE("Binary arithmetic golden payloads", "[arithmetic][binary]")
{
  // Calculated independently using unbounded integer interval endpoints and
  // a textual bit stream. The skewed vectors exercise E3 as well as E1/E2.
  auto golden =
    [](std::vector<unsigned> const &bits, binary_model probabilities, std::vector<std::byte> const &expected) {
      REQUIRE(encode(bits, probabilities) == expected);
      REQUIRE(encode(bits, probabilities) == expected);
      check(bits, probabilities, expected);
    };
  golden({}, { 1, 1 }, bytes({ 0x40, 0, 0, 0 }));
  golden({ 0 }, { 1, 1 }, bytes({ 0x20, 0, 0, 0, 0 }));
  golden({ 1 }, { 1, 1 }, bytes({ 0xa0, 0, 0, 0, 0 }));
  golden({ 0, 1, 1, 0, 1, 0, 0, 1 }, { 1, 1 }, bytes({ 0x69, 0x40, 0, 0, 0 }));
  golden({ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1 }, { 1, 2 }, bytes({ 0x76, 0x1c, 0, 0, 0, 0 }));
  std::vector<unsigned> alternating;
  for (unsigned index = 0; index < 40; ++index) { alternating.push_back(index % 2); }
  golden(alternating, { 3, 5 }, bytes({ 0x2f, 0x05, 0x39, 0x78, 0x29, 0xc0, 0, 0, 0 }));
  auto rare_zero = std::vector<std::byte>(40, std::byte{ 0 });
  rare_zero.insert(rare_zero.end(), { std::byte{ 0x40 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
  golden(std::vector<unsigned>(20, 0), { 1, 65535 }, rare_zero);
  auto rare_one = std::vector<std::byte>(40, std::byte{ 0xff });
  rare_one.insert(rare_one.end(), { std::byte{ 0x40 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
  golden(std::vector<unsigned>(20, 1), { 65535, 1 }, rare_one);
}

TEST_CASE("Binary arithmetic exhausts short sequences and probability extremes", "[arithmetic][binary]")
{
  std::array const models{ binary_model{ 1, 1 },
    binary_model{ 1, 2 },
    binary_model{ 3, 5 },
    binary_model{ 1, 65535 },
    binary_model{ 65535, 1 },
    binary_model{ 32768, 32768 } };
  for (auto probabilities : models) {
    for (unsigned length = 0; length <= 9; ++length) {
      for (unsigned pattern = 0; pattern < (1U << length); ++pattern) {
        std::vector<unsigned> bits;
        for (unsigned index = 0; index < length; ++index) { bits.push_back((pattern >> index) & 1U); }
        auto const payload = encode(bits, probabilities);
        check(bits, probabilities, payload);
      }
    }
  }
}

TEST_CASE("Binary arithmetic emits long pending runs and accepts changing models", "[arithmetic][binary]")
{
  // Each pair maps the full interval into its middle half, restoring the full
  // interval via E3 without emitting anything. The final E1/E2 flushes the run.
  for (bool flush_bit : { false, true }) {
    arithmetic::binary_arithmetic_encoder encoder;
    for (unsigned index = 0; index < 4096; ++index) {
      encoder.encode(true, { 1, 3 });
      encoder.encode(false, { 2, 1 });
    }
    encoder.encode(flush_bit, { 1, 1 });
    auto const payload = encoder.finish();
    REQUIRE(payload.front() == (flush_bit ? std::byte{ 0x80 } : std::byte{ 0x7f }));
    arithmetic::binary_arithmetic_decoder decoder(payload);
    for (unsigned index = 0; index < 4096; ++index) {
      REQUIRE(decoder.decode({ 1, 3 }));
      REQUIRE_FALSE(decoder.decode({ 2, 1 }));
    }
    REQUIRE(decoder.decode({ 1, 1 }) == flush_bit);
    decoder.finish();
  }

  std::mt19937 random(182);
  std::vector<binary_model> models;
  std::vector<bool> bits;
  arithmetic::binary_arithmetic_encoder encoder;
  for (unsigned index = 0; index < 20000; ++index) {
    auto const zero = 1U + static_cast<std::uint32_t>(random() % 65535);
    models.push_back({ zero, 65536 - zero });
    bits.push_back((random() & 1U) != 0);
    encoder.encode(bits.back(), models.back());
  }
  auto const payload = encoder.finish();
  arithmetic::binary_arithmetic_decoder decoder(payload);
  for (std::size_t index = 0; index < bits.size(); ++index) {
    REQUIRE(decoder.decode(models.at(index)) == bits.at(index));
  }
  decoder.finish();
}

TEST_CASE("Binary model counts bits and byte helpers preserve MSB-first order", "[arithmetic][binary]")
{
  auto const uniform = arithmetic::make_binary_model(std::vector<std::byte>{});
  REQUIRE(uniform.zero_frequency_ == 2048);
  REQUIRE(uniform.one_frequency_ == 2048);
  auto const zeros = arithmetic::make_binary_model(bytes({ 0 }));
  REQUIRE(zeros.zero_frequency_ == 3686);
  REQUIRE(zeros.one_frequency_ == 410);
  auto const ones = arithmetic::make_binary_model(bytes({ 255 }));
  REQUIRE(ones.zero_frequency_ == zeros.one_frequency_);
  REQUIRE(ones.one_frequency_ == zeros.zero_frequency_);
  std::vector<signed char> const signed_bytes{ -128, -1, 0 };
  auto const signed_model = arithmetic::make_binary_model(signed_bytes);
  auto const byte_model = arithmetic::make_binary_model(bytes({ 128, 255, 0 }));
  REQUIRE(signed_model.zero_frequency_ == byte_model.zero_frequency_);
  REQUIRE(signed_model.one_frequency_ == byte_model.one_frequency_);
  for (unsigned precision : { 1U, 12U, 16U }) {
    auto const probabilities = arithmetic::make_binary_model(bytes({ 0, 0, 255 }), precision);
    REQUIRE(probabilities.total() == (1U << precision));
    REQUIRE(probabilities.zero_frequency_ >= probabilities.one_frequency_);
    REQUIRE(probabilities.one_frequency_ != 0);
  }

  arithmetic::binary_arithmetic_encoder golden;
  golden.encode_byte(std::byte{ 0x69 }, { 1, 1 });
  REQUIRE(golden.finish() == bytes({ 0x69, 0x40, 0, 0, 0 }));
  std::mt19937 random(63);
  std::vector<std::byte> input;
  for (unsigned index = 0; index < 20000; ++index) { input.push_back(static_cast<std::byte>(random() & 255U)); }
  for (unsigned index = 0; index < 256; ++index) { input.push_back(static_cast<std::byte>(index)); }
  auto const probabilities = arithmetic::make_binary_model(input);
  arithmetic::binary_arithmetic_encoder encoder;
  for (auto byte : input) { encoder.encode_byte(byte, probabilities); }
  auto const payload = encoder.finish();
  arithmetic::binary_arithmetic_decoder decoder(payload);
  for (auto byte : input) { REQUIRE(decoder.decode_byte(probabilities) == byte); }
  decoder.finish();
}

TEST_CASE("Binary model consumes single-pass stream input", "[arithmetic][binary]")
{
  for (auto const &input : { std::string{}, std::string{ "single pass model" }, std::string{ '\0', '\x80', '\xff' } }) {
    std::istringstream stream(input);
    auto source = std::ranges::subrange(std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{});
    static_assert(std::ranges::input_range<decltype(source)>);
    static_assert(!std::ranges::forward_range<decltype(source)>);
    auto const actual = arithmetic::make_binary_model(source);
    auto const expected = arithmetic::make_binary_model(input);
    REQUIRE(actual.zero_frequency_ == expected.zero_frequency_);
    REQUIRE(actual.one_frequency_ == expected.one_frequency_);
    REQUIRE(std::istreambuf_iterator<char>{ stream } == std::istreambuf_iterator<char>{});
  }
}

TEST_CASE("Binary model rejects invalid precision before consuming stream input", "[arithmetic][binary]")
{
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_argument);
  std::string const input{ '\0', '\x80', 'A', '\xff' };
  for (unsigned precision : { 0U, 17U, std::numeric_limits<unsigned>::max() }) {
    std::istringstream stream(input);
    auto source = std::ranges::subrange(std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{});
    REQUIRE_THROWS_MATCHES(arithmetic::make_binary_model(source, precision), dacoexec::exception, invalid);
    REQUIRE(stream.tellg() == std::streampos{ 0 });
    std::string remaining;
    std::getline(stream, remaining);
    REQUIRE(remaining == input);
  }
}

TEST_CASE("Binary arithmetic validates models and finalized use", "[arithmetic][binary]")
{
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_argument);
  auto const finished = dacoexec::test::error_is(dacoexec::errc::invalid_state);
  auto const payload = bytes({ 0x40, 0, 0, 0 });
  arithmetic::binary_arithmetic_encoder encoder;
  arithmetic::binary_arithmetic_decoder decoder(payload);
  for (auto probabilities : { binary_model{ 0, 1 },
         binary_model{ 1, 0 },
         binary_model{ 65536, 1 },
         binary_model{ 1, 65536 },
         binary_model{ std::numeric_limits<std::uint32_t>::max(), 1 } }) {
    REQUIRE_THROWS_MATCHES(encoder.encode(false, probabilities), dacoexec::exception, invalid);
    REQUIRE_THROWS_MATCHES(decoder.decode(probabilities), dacoexec::exception, invalid);
  }
  for (unsigned precision : { 0U, 17U }) {
    REQUIRE_THROWS_MATCHES(arithmetic::make_binary_model(bytes({ 0 }), precision), dacoexec::exception, invalid);
  }
  REQUIRE(encoder.finish() == payload);
  decoder.finish();
  REQUIRE_THROWS_MATCHES(encoder.finish(), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(encoder.encode(false, {}), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(encoder.encode_byte(std::byte{ 0 }, {}), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(decoder.finish(), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(decoder.decode({}), dacoexec::exception, finished);
  REQUIRE_THROWS_MATCHES(decoder.decode_byte({}), dacoexec::exception, finished);
}

TEST_CASE("Binary arithmetic checks exact termination and rejects truncation", "[arithmetic][binary]")
{
  auto const invalid = dacoexec::test::error_is(dacoexec::errc::invalid_payload);
  for (unsigned length = 0; length < 40; ++length) {
    std::vector<unsigned> bits;
    for (unsigned index = 0; index < length; ++index) { bits.push_back(index % 2); }
    auto const payload = encode(bits, { 3, 5 });
    for (std::size_t size = 0; size < payload.size(); ++size) {
      REQUIRE_THROWS_MATCHES(
        consume(bits.size(), { 3, 5 }, std::span(payload).first(size)), dacoexec::exception, invalid);
    }
    auto trailing = payload;
    trailing.push_back(std::byte{ 0 });
    REQUIRE_THROWS_MATCHES(consume(bits.size(), { 3, 5 }, trailing), dacoexec::exception, invalid);
    auto bad_terminal = payload;
    bad_terminal.back() ^= std::byte{ 1 };
    REQUIRE_THROWS_MATCHES(consume(bits.size(), { 3, 5 }, bad_terminal), dacoexec::exception, invalid);
  }
  auto const payload = encode(std::array{ 0U, 1U, 0U }, { 1, 1 });
  arithmetic::binary_arithmetic_decoder early(payload);
  REQUIRE_THROWS_MATCHES(early.finish(), dacoexec::exception, invalid);
  arithmetic::binary_arithmetic_decoder late(payload);
  for (unsigned index = 0; index < 4; ++index) { static_cast<void>(late.decode({ 1, 1 })); }
  REQUIRE_THROWS_MATCHES(late.finish(), dacoexec::exception, invalid);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
