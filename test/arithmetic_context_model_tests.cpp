#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <dacoexec/detail/arithmetic/binary/context/model.hpp>
#include <dacoexec/detail/arithmetic/binary/decoder.hpp>
#include <dacoexec/detail/arithmetic/binary/encoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <span>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace arithmetic = dacoexec::detail::arithmetic;

auto bytes(std::initializer_list<unsigned> values) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  for (auto value : values) { result.push_back(static_cast<std::byte>(value)); }
  return result;
}

void check_uniform(arithmetic::context_model const &model)
{
  REQUIRE(model.context_index() == 0);
  for (auto const &context : model.contexts()) {
    REQUIRE(context.zero_frequency_ == 1);
    REQUIRE(context.one_frequency_ == 1);
  }
}

auto encode(std::span<std::byte const> input, arithmetic::context_model &model) -> std::vector<std::byte>
{
  arithmetic::binary_arithmetic_encoder encoder;
  for (auto byte : input) {
    for (unsigned remaining = 8; remaining != 0; --remaining) {
      auto const bit = ((std::to_integer<unsigned>(byte) >> (remaining - 1)) & 1U) != 0;
      encoder.encode(bit, model.probabilities());
      model.update(bit);
    }
  }
  return encoder.finish();
}
}// namespace

TEST_CASE("Binary contexts have independent nonempty golden payloads", "[arithmetic][model][context]")
{
  // Calculated separately with Python unbounded integer interval endpoints,
  // explicit per-context count pairs, and an unpacked output-bit list.
  auto golden = [](std::vector<std::byte> const &input, std::vector<std::byte> const &payload) {
    arithmetic::context_model encoding;
    REQUIRE(encode(input, encoding) == payload);
    arithmetic::context_model decoding;
    arithmetic::binary_arithmetic_decoder decoder(payload);
    for (auto expected : input) {
      unsigned byte = 0;
      for (unsigned index = 0; index < 8; ++index) {
        auto const bit = decoder.decode(decoding.probabilities());
        decoding.update(bit);
        byte = byte * 2 + static_cast<unsigned>(bit);
      }
      REQUIRE(static_cast<std::byte>(byte) == expected);
    }
    decoder.finish();
  };
  golden(bytes({ 0x00 }), bytes({ 0x00, 0x40, 0, 0, 0 }));
  golden(bytes({ 0xff }), bytes({ 0xff, 0x40, 0, 0, 0 }));
  golden(bytes({ 0x69 }), bytes({ 0x69, 0x40, 0, 0, 0 }));
  golden(bytes({ 0x00, 0x00 }), bytes({ 0x00, 0x04, 0, 0, 0, 0 }));
  golden(bytes({ 0xff, 0xff }), bytes({ 0xff, 0xfc, 0, 0, 0, 0 }));
  golden(bytes({ 0x69, 0x69 }), bytes({ 0x69, 0x62, 0, 0, 0, 0 }));
  golden(bytes({ 0xaa, 0x55 }), bytes({ 0xaa, 0x55, 0x40, 0, 0, 0 }));
  golden(bytes({ 0xff, 0x00 }), bytes({ 0xff, 0x00, 0x40, 0, 0, 0 }));
}

TEST_CASE("Binary contexts select position and history across byte boundaries", "[arithmetic][model][context]")
{
  arithmetic::context_model model;
  check_uniform(model);
  // MSB-first 0x69, then the first two bits of the next byte.
  std::array const bits{ false, true, true, false, true, false, false, true, true, false };
  std::array<std::size_t, 11> const indices{ 0, 4, 9, 15, 18, 21, 26, 28, 1, 7, 10 };
  for (std::size_t index = 0; index < bits.size(); ++index) {
    REQUIRE(model.context_index() == indices.at(index));
    auto const before = model.contexts();
    model.update(bits.at(index));
    for (std::size_t context = 0; context < before.size(); ++context) {
      auto const selected = context == indices.at(index);
      REQUIRE(model.contexts().at(context).zero_frequency_
              == before.at(context).zero_frequency_ + (selected && !bits.at(index) ? 1U : 0U));
      REQUIRE(model.contexts().at(context).one_frequency_
              == before.at(context).one_frequency_ + (selected && bits.at(index) ? 1U : 0U));
    }
  }
  REQUIRE(model.context_index() == indices.back());
  model.reset();
  check_uniform(model);
}

TEST_CASE("Binary contexts rescale independently strictly above the threshold", "[arithmetic][model][context]")
{
  arithmetic::context_model model;
  // A zero stream visits contexts 0,4,...,28 in order.
  for (unsigned index = 0; index < (32768U - 2U) * 8U; ++index) { model.update(false); }
  REQUIRE(model.context_index() == 0);
  REQUIRE(model.probabilities().zero_frequency_ == 32767);
  REQUIRE(model.probabilities().total() == 32768);
  model.update(false);
  REQUIRE(model.contexts().at(0).zero_frequency_ == 16384);
  REQUIRE(model.contexts().at(0).one_frequency_ == 1);
  REQUIRE(model.contexts().at(4).total() == 32768);
  for (std::size_t index = 0; index < model.contexts().size(); ++index) {
    if (index % 4 != 0) {
      REQUIRE(model.contexts().at(index).zero_frequency_ == 1);
      REQUIRE(model.contexts().at(index).one_frequency_ == 1);
    }
  }
  // The formerly unseen outcome still has positive width.
  model.update(true);
  REQUIRE(model.contexts().at(4).zero_frequency_ == 16384);
  REQUIRE(model.contexts().at(4).one_frequency_ == 1);
}

TEST_CASE("Binary context evolution matches an independent reference", "[arithmetic][model][context]")
{
  auto const repeated_bit = GENERATE(false, true);
  arithmetic::context_model model;
  std::array<std::array<std::uint32_t, 2>, 32> expected;
  expected.fill({ 1, 1 });
  std::mt19937 random(1729);
  unsigned history = 0;
  unsigned rescales = 0;
  std::array<bool, 32> visited{};
  for (unsigned index = 0; index < 700000; ++index) {
    auto const bit = index < 600000 ? repeated_bit : (random() & 1U) != 0;
    auto const selected = (index % 8) * 4 + history;
    REQUIRE(model.context_index() == selected);
    REQUIRE(model.probabilities().zero_frequency_ == expected.at(selected).at(0));
    REQUIRE(model.probabilities().one_frequency_ == expected.at(selected).at(1));
    visited.at(selected) = true;
    auto &counts = expected.at(selected);
    ++counts.at(bit ? 1U : 0U);
    if (counts.at(0) + counts.at(1) > 32768) {
      for (auto &count : counts) { count = count / 2 + count % 2; }
      ++rescales;
    }
    model.update(bit);
    history = (history * 2 + (bit ? 1U : 0U)) % 4;
  }
  REQUIRE(rescales >= 24);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(visited.at(index));
    REQUIRE(model.contexts().at(index).zero_frequency_ == expected.at(index).at(0));
    REQUIRE(model.contexts().at(index).one_frequency_ == expected.at(index).at(1));
  }
}

TEST_CASE("Binary contexts track alternating bytes across many boundaries", "[arithmetic][model][context]")
{
  arithmetic::context_model model;
  std::array<std::array<std::uint32_t, 2>, 32> expected;
  expected.fill({ 1, 1 });
  // Repeat bytes AA 55. Derive history directly from previous source bits,
  // independently of the model's rolling history and position state.
  auto bit_at = [](unsigned index) { return (0xaa55U >> (15U - index % 16U)) & 1U; };
  for (unsigned index = 0; index < 65536; ++index) {
    auto const history = (index >= 2 ? 2U * bit_at(index - 2) : 0U) + (index >= 1 ? bit_at(index - 1) : 0U);
    auto const context = 4U * (index % 8U) + history;
    REQUIRE(model.context_index() == context);
    REQUIRE(model.probabilities().zero_frequency_ == expected.at(context).at(0));
    REQUIRE(model.probabilities().one_frequency_ == expected.at(context).at(1));
    auto const bit = bit_at(index);
    ++expected.at(context).at(bit);
    model.update(bit != 0);
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(model.contexts().at(index).zero_frequency_ == expected.at(index).at(0));
    REQUIRE(model.contexts().at(index).one_frequency_ == expected.at(index).at(1));
  }
}

TEST_CASE("Binary contexts synchronize with the existing arithmetic engine", "[arithmetic][model][context]")
{
  std::vector<std::byte> input(80000, std::byte{ 0 });
  std::mt19937 random(42);
  for (unsigned index = 0; index < 10000; ++index) { input.push_back(static_cast<std::byte>(random() & 255U)); }
  arithmetic::context_model encoding;
  auto const payload = encode(input, encoding);
  arithmetic::context_model decoding;
  arithmetic::binary_arithmetic_decoder decoder(payload);
  for (auto expected : input) {
    unsigned byte = 0;
    for (unsigned index = 0; index < 8; ++index) {
      auto const bit = decoder.decode(decoding.probabilities());
      decoding.update(bit);
      byte = (byte << 1U) | static_cast<unsigned>(bit);
    }
    REQUIRE(static_cast<std::byte>(byte) == expected);
  }
  decoder.finish();
  REQUIRE(decoding.context_index() == encoding.context_index());
  for (std::size_t index = 0; index < encoding.contexts().size(); ++index) {
    REQUIRE(decoding.contexts().at(index).zero_frequency_ == encoding.contexts().at(index).zero_frequency_);
    REQUIRE(decoding.contexts().at(index).one_frequency_ == encoding.contexts().at(index).one_frequency_);
  }
  encoding.reset();
  REQUIRE(encode(input, encoding) == payload);
  arithmetic::context_model fresh;
  REQUIRE(encode(input, fresh) == payload);
  fresh.reset();
  REQUIRE(
    encode({}, fresh) == std::vector<std::byte>{ std::byte{ 0x40 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
