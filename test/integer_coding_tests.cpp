#include <dacoexec/bit.hpp>
#include <dacoexec/detail/integer_coding/common.hpp>
#include <dacoexec/detail/integer_coding/delta.hpp>
#include <dacoexec/detail/integer_coding/gamma.hpp>
#include <dacoexec/detail/integer_coding/golomb.hpp>
#include <dacoexec/detail/integer_coding/rice.hpp>
#include <dacoexec/detail/support/byte.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

constexpr auto byte_bits = dacoexec::detail::support::bits_per_byte;
constexpr auto all_bits_byte = std::byte{ 0xFF };
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
constexpr auto sample_value = std::uint64_t{ 13 };
constexpr auto rice_shift = 2U;
constexpr auto golomb_divisor = 3U;
constexpr auto random_seed = 0xC0FFEEU;
constexpr auto random_count = 256U;
constexpr auto small_value_limit = 1024U;
constexpr auto largest_shift = dacoexec::detail::integer_coding::integer_bits - 1U;

struct encoded_value
{
  std::vector<std::byte> bytes_;
  std::size_t bits_ = 0;
};

template<class Encode> auto encode_value(Encode encode, std::uint64_t value) -> encoded_value
{
  encoded_value result;
  dacoexec::bit_writer writer{ std::back_inserter(result.bytes_) };
  encode(writer, value);
  auto const padding = writer.finish();
  result.bits_ = (result.bytes_.size() * byte_bits) - padding;
  return result;
}

auto const gamma_write = [](auto &out, std::uint64_t value) -> void {
  dacoexec::detail::integer_coding::write_gamma(out, value);
};
auto const gamma_read = [](auto &reader) -> std::uint64_t {
  return dacoexec::detail::integer_coding::read_gamma(reader);
};
auto const delta_write = [](auto &out, std::uint64_t value) -> void {
  dacoexec::detail::integer_coding::write_delta(out, value);
};
auto const delta_read = [](auto &reader) -> std::uint64_t {
  return dacoexec::detail::integer_coding::read_delta(reader);
};
auto const rice_write = [](auto &out, std::uint64_t value) -> void {
  dacoexec::detail::integer_coding::write_rice(out, value, rice_shift);
};
auto const rice_read = [](auto &reader) -> std::uint64_t {
  return dacoexec::detail::integer_coding::read_rice(reader, rice_shift);
};
auto const golomb_write = [](auto &out, std::uint64_t value) -> void {
  dacoexec::detail::integer_coding::write_golomb(out, value, golomb_divisor);
};
auto const golomb_read = [](auto &reader) -> std::uint64_t {
  return dacoexec::detail::integer_coding::read_golomb(reader, golomb_divisor);
};

template<class Encode, class Decode>
auto check_value(Encode encode, Decode decode, std::uint64_t value, std::string_view golden = {}) -> void
{
  auto const packed = encode_value(encode, value);
  dacoexec::bit_reader reader{ packed.bytes_, packed.bits_ };
  REQUIRE(decode(reader) == value);
  REQUIRE(reader.remaining_bits() == 0U);
  if (!golden.empty()) {
    REQUIRE(packed.bits_ == golden.size());
    dacoexec::bit_reader bits{ packed.bytes_, packed.bits_ };
    for (char const bit : golden) { REQUIRE(bits.read_bit() == (bit == '1')); }
  }
  for (std::size_t limit = 0; limit < packed.bits_; ++limit) {
    dacoexec::bit_reader truncated{ packed.bytes_, limit };
    REQUIRE_THROWS_AS(decode(truncated), std::out_of_range);
  }
}

// A fixed byte destination makes primitive compile-time round trips possible
// without changing the existing runtime bit_writer contract.
struct fixed_writer
{
  std::array<std::byte, dacoexec::detail::integer_coding::integer_bits> bytes_{};
  std::size_t bits_ = 0;

  constexpr auto write_bit(bool bit) -> void
  {
    if (bit) {
      auto const shift = byte_bits - 1U - static_cast<unsigned>(bits_ % byte_bits);
      bytes_.at(bits_ / byte_bits) |= static_cast<std::byte>(1U << shift);
    }
    ++bits_;
  }

  constexpr auto write_bits(dacoexec::bit_sequence bits) -> void
  {
    for (unsigned index = bits.bit_count_; index != 0U; --index) {
      write_bit(((bits.value_ >> (index - 1U)) & 1U) != 0U);
    }
  }
};

constexpr auto constexpr_round_trip() -> bool
{
  fixed_writer writer;
  dacoexec::detail::integer_coding::write_gamma(writer, maximum);
  dacoexec::detail::integer_coding::write_delta(writer, maximum);
  dacoexec::detail::integer_coding::write_rice(writer, maximum, largest_shift);
  dacoexec::detail::integer_coding::write_golomb(writer, sample_value, golomb_divisor);
  dacoexec::bit_reader reader{ writer.bytes_, writer.bits_ };
  return dacoexec::detail::integer_coding::read_gamma(reader) == maximum
         && dacoexec::detail::integer_coding::read_delta(reader) == maximum
         && dacoexec::detail::integer_coding::read_rice(reader, largest_shift) == maximum
         && dacoexec::detail::integer_coding::read_golomb(reader, golomb_divisor) == sample_value
         && reader.remaining_bits() == 0U;
}

static_assert(constexpr_round_trip());
static_assert(dacoexec::detail::integer_coding::gamma_bit_length(sample_value) == std::string_view{ "0001101" }.size());
static_assert(
  dacoexec::detail::integer_coding::delta_bit_length(sample_value) == std::string_view{ "00100101" }.size());

}// namespace

TEST_CASE("integer coding golden codewords and every truncated prefix", "[integer_coding]")
{
  check_value(gamma_write, gamma_read, 1U, "1");
  check_value(gamma_write, gamma_read, sample_value, "0001101");
  check_value(delta_write, delta_read, 1U, "1");
  check_value(delta_write, delta_read, sample_value, "00100101");
  check_value(rice_write, rice_read, 0U, "000");
  check_value(rice_write, rice_read, sample_value, "111001");
  check_value(rice_write, rice_read, std::uint64_t{ 1 } << rice_shift, "1000");
  check_value(golomb_write, golomb_read, 0U, "00");
  check_value(golomb_write, golomb_read, sample_value, "1111010");
}

TEST_CASE("integer coding integer boundaries and randomized values", "[integer_coding]")
{
  for (unsigned shift = 0; shift < dacoexec::detail::integer_coding::integer_bits; ++shift) {
    auto const power = std::uint64_t{ 1 } << shift;
    for (auto const value : std::array{ power, power - 1U, maximum }) {
      if (value != 0U) {
        check_value(gamma_write, gamma_read, value);
        check_value(delta_write, delta_read, value);
      }
    }
  }
  check_value(
    [](auto &out, std::uint64_t value) -> void {
      dacoexec::detail::integer_coding::write_rice(out, value, largest_shift);
    },
    [](auto &reader) -> std::uint64_t { return dacoexec::detail::integer_coding::read_rice(reader, largest_shift); },
    maximum);
  std::mt19937_64 random{ random_seed };
  for (unsigned index = 0; index < random_count; ++index) {
    auto const value = random();
    if (value != 0U) {
      check_value(gamma_write, gamma_read, value);
      check_value(delta_write, delta_read, value);
    }
    check_value(rice_write, rice_read, value % small_value_limit);
    check_value(golomb_write, golomb_read, value % small_value_limit);
  }
}

TEST_CASE("Rice and Golomb agree for power of two divisors", "[integer_coding]")
{
  for (unsigned shift = 0; shift < byte_bits; ++shift) {
    for (std::uint64_t value = 0; value < small_value_limit; ++value) {
      auto const rice = encode_value(
        [shift](
          auto &out, std::uint64_t input) -> void { dacoexec::detail::integer_coding::write_rice(out, input, shift); },
        value);
      auto const golomb = encode_value(
        [shift](auto &out, std::uint64_t input) -> void {
          dacoexec::detail::integer_coding::write_golomb(out, input, std::uint32_t{ 1 } << shift);
        },
        value);
      REQUIRE(rice.bits_ == golomb.bits_);
      REQUIRE(rice.bytes_ == golomb.bytes_);
    }
  }
  for (auto const divisor : std::array{ 1U, golomb_divisor, std::numeric_limits<std::uint32_t>::max() }) {
    auto const encode = [divisor](auto &out, std::uint64_t value) -> void {
      dacoexec::detail::integer_coding::write_golomb(out, value, divisor);
    };
    auto const decode = [divisor](auto &reader) -> std::uint64_t {
      return dacoexec::detail::integer_coding::read_golomb(reader, divisor);
    };
    check_value(encode, decode, divisor - 1U);
    if (divisor != 1U) { check_value(encode, decode, divisor); }
  }
}

TEST_CASE("integer coding invalid domains and bounded malicious prefixes", "[integer_coding]")
{
  std::vector<std::byte> output;
  dacoexec::bit_writer writer{ std::back_inserter(output) };
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::write_gamma(writer, 0U), std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::write_delta(writer, 0U), std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::write_golomb(writer, 0U, 0U), std::invalid_argument);
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::write_rice(writer, 0U, dacoexec::detail::integer_coding::integer_bits),
    std::invalid_argument);
  REQUIRE(writer.finish() == 0U);
  REQUIRE(output.empty());

  std::array<std::byte, 1> const ones{ std::byte{ 0xFF } };
  dacoexec::bit_reader limited{ ones, byte_bits };
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::read_rice(limited, 0U, { .max_unary_bits_ = rice_shift }), std::length_error);
  dacoexec::bit_reader overflow{ ones, byte_bits };
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_rice(overflow, largest_shift), std::overflow_error);
  dacoexec::bit_reader invalid{ ones, byte_bits };
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::read_rice(invalid, dacoexec::detail::integer_coding::integer_bits),
    std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_golomb(invalid, 0U), std::invalid_argument);
  REQUIRE(invalid.remaining_bits() == byte_bits);

  std::array<std::byte, sizeof(std::uint64_t)> const zeros{};
  dacoexec::bit_reader gamma_overflow{ zeros, dacoexec::detail::integer_coding::integer_bits };
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_gamma(gamma_overflow), std::overflow_error);
  auto const wide_length = encode_value(gamma_write, dacoexec::detail::integer_coding::integer_bits + 1U);
  dacoexec::bit_reader delta_overflow{ wide_length.bytes_, wide_length.bits_ };
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_delta(delta_overflow), std::overflow_error);
}

TEST_CASE("bit reader preserves exact boundaries and failed reads", "[bit_reader]")
{
  constexpr auto valid_bits = 3U;
  constexpr auto expected_value = 0b101U;
  std::array<std::byte, 1> const bytes{ std::byte{ 0b10111111 } };
  dacoexec::bit_reader reader{ bytes, valid_bits };
  REQUIRE(reader.read_bits(0U) == 0U);
  REQUIRE_THROWS_AS(reader.read_bits(valid_bits + 1U), std::out_of_range);
  REQUIRE_THROWS_AS(reader.read_bits(dacoexec::detail::integer_coding::integer_bits + 1U), std::invalid_argument);
  REQUIRE(reader.remaining_bits() == valid_bits);
  REQUIRE(reader.read_bits(valid_bits) == expected_value);
  REQUIRE_THROWS_AS(reader.read_bit(), std::out_of_range);
  REQUIRE_THROWS_AS((dacoexec::bit_reader{ bytes, byte_bits + 1U }), std::invalid_argument);
  dacoexec::bit_reader empty{ std::span<std::byte const>{}, 0U };
  REQUIRE(empty.read_bits(0U) == 0U);
  REQUIRE_THROWS_AS(empty.read_bit(), std::out_of_range);
  std::array<std::byte, sizeof(std::uint64_t)> full{};
  full.fill(all_bits_byte);
  dacoexec::bit_reader wide{ full, dacoexec::detail::integer_coding::integer_bits };
  REQUIRE(wide.read_bits(dacoexec::detail::integer_coding::integer_bits) == maximum);
}

TEST_CASE("unary limits are inclusive and default decoding is unrestricted", "[integer_coding]")
{
  constexpr auto former_default_limit = std::uint64_t{ 1'048'576 };
  for (auto const limit : std::array{ std::uint64_t{ 0 }, std::uint64_t{ 1 }, former_default_limit }) {
    for (auto const quotient : std::array{ limit, limit + 1U }) {
      auto const encode = [](auto &out, std::uint64_t value) -> void {
        dacoexec::detail::integer_coding::write_rice(out, value, 0U);
      };
      auto const packed = encode_value(encode, quotient);
      for (bool const golomb : std::array{ false, true }) {
        auto const decode = [golomb](
                              auto &reader, dacoexec::detail::integer_coding::decode_limits limits) -> std::uint64_t {
          return golomb ? dacoexec::detail::integer_coding::read_golomb(reader, 1U, limits)
                        : dacoexec::detail::integer_coding::read_rice(reader, 0U, limits);
        };
        dacoexec::bit_reader bounded{ packed.bytes_, packed.bits_ };
        if (quotient == limit) {
          REQUIRE(decode(bounded, { .max_unary_bits_ = limit }) == quotient);
          REQUIRE(bounded.remaining_bits() == 0U);
        } else {
          REQUIRE_THROWS_AS(decode(bounded, { .max_unary_bits_ = limit }), std::length_error);
        }
        dacoexec::bit_reader unrestricted{ packed.bytes_, packed.bits_ };
        REQUIRE(decode(unrestricted, {}) == quotient);
        REQUIRE(unrestricted.remaining_bits() == 0U);
      }
    }
  }
}

TEST_CASE("Rice and Golomb sizes use checked arithmetic", "[integer_coding]")
{
  static_assert(dacoexec::detail::integer_coding::rice_bit_length(maximum - 1U, 0U) == maximum);
  static_assert(dacoexec::detail::integer_coding::golomb_bit_length(maximum - 1U, 1U) == maximum);
  static_assert(dacoexec::detail::integer_coding::rice_bit_length(maximum, largest_shift) == largest_shift + 2U);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::rice_bit_length(maximum, 0U), std::overflow_error);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::golomb_bit_length(maximum, 1U), std::overflow_error);
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::rice_bit_length(0U, dacoexec::detail::integer_coding::integer_bits),
    std::invalid_argument);
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::golomb_bit_length(0U, 0U), std::invalid_argument);
  for (std::uint64_t value = 0; value < small_value_limit; ++value) {
    REQUIRE(
      dacoexec::detail::integer_coding::rice_bit_length(value, rice_shift) == encode_value(rice_write, value).bits_);
    REQUIRE(dacoexec::detail::integer_coding::golomb_bit_length(value, golomb_divisor)
            == encode_value(golomb_write, value).bits_);
  }
}

TEST_CASE("mixed integer coding sequences share one bitstream", "[integer_coding]")
{
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  std::mt19937_64 random{ random_seed };
  std::array<std::uint64_t, random_count> values{};
  for (auto &value : values) {
    value = random() % small_value_limit;
    dacoexec::detail::integer_coding::write_gamma(writer, value + 1U);
    dacoexec::detail::integer_coding::write_rice(writer, value, rice_shift);
    dacoexec::detail::integer_coding::write_delta(writer, value + 1U);
    dacoexec::detail::integer_coding::write_golomb(writer, value, golomb_divisor);
  }
  auto const padding = writer.finish();
  dacoexec::bit_reader reader{ bytes, (bytes.size() * byte_bits) - padding };
  for (auto const value : values) {
    REQUIRE(dacoexec::detail::integer_coding::read_gamma(reader) == value + 1U);
    REQUIRE(dacoexec::detail::integer_coding::read_rice(reader, rice_shift) == value);
    REQUIRE(dacoexec::detail::integer_coding::read_delta(reader) == value + 1U);
    REQUIRE(dacoexec::detail::integer_coding::read_golomb(reader, golomb_divisor) == value);
  }
  REQUIRE(reader.remaining_bits() == 0U);
}

TEST_CASE("non-power-of-two Golomb golden remainders", "[integer_coding]")
{
  // For divisor=5, b=3 and cutoff=3: remainders 0,2 use two bits;
  // remainders 3,4 use three bits. Quotient one adds the prefix 10.
  constexpr auto divisor = 5U;
  auto const encode = [](auto &out, std::uint64_t value) -> void {
    dacoexec::detail::integer_coding::write_golomb(out, value, divisor);
  };
  auto const decode = [](auto &reader) -> std::uint64_t {
    return dacoexec::detail::integer_coding::read_golomb(reader, divisor);
  };
  check_value(encode, decode, 0U, "000");
  check_value(encode, decode, 2U, "010");
  check_value(encode, decode, 3U, "0110");
  check_value(encode, decode, divisor - 1U, "0111");
  check_value(encode, decode, divisor, "1000");
  check_value(encode, decode, divisor + 3U, "10110");
}

TEST_CASE("Golomb decoder arithmetic rejects values beyond uint64_t", "[integer_coding]")
{
  // Even the largest uint32_t divisor needs billions of unary bits near
  // UINT64_MAX. Exercise the decoder's reconstruction separately from input.
  constexpr auto divisor = std::numeric_limits<std::uint32_t>::max() - 1U;
  constexpr auto quotient = maximum / divisor;
  constexpr auto remainder = maximum % divisor;
  static_assert(remainder + 1U < divisor);
  REQUIRE(dacoexec::detail::integer_coding::golomb_value({ .quotient_ = quotient, .remainder_ = remainder }, divisor)
          == maximum);
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::golomb_value({ .quotient_ = quotient, .remainder_ = remainder + 1U }, divisor),
    std::overflow_error);
  REQUIRE_THROWS_AS(
    dacoexec::detail::integer_coding::golomb_value({ .quotient_ = quotient + 1U, .remainder_ = 0U }, divisor),
    std::overflow_error);

  // Exercise the unary quotient overflow branch with a small bound.
  std::array<std::byte, 1> const prefix{ std::byte{ 0b11000000 } };
  dacoexec::bit_reader reader{ prefix, byte_bits };
  REQUIRE_THROWS_AS(dacoexec::detail::integer_coding::read_unary(reader, {}, 1U), std::overflow_error);
}

namespace {

struct failing_byte_sink
{
  using value_type = std::byte;
  std::vector<std::byte> bytes_;

  auto push_back(std::byte value) -> void
  {
    if (!bytes_.empty()) { throw std::runtime_error("integer coding test sink failure"); }
    bytes_.push_back(value);
  }
};

template<class Encode> auto check_sink_failure(Encode encode, std::uint64_t value) -> void
{
  failing_byte_sink sink;
  dacoexec::bit_writer writer{ std::back_inserter(sink) };
  REQUIRE_THROWS_WITH(encode(writer, value), "integer coding test sink failure");
  REQUIRE(sink.bytes_.size() == 1U);
  REQUIRE_THROWS_AS(writer.finish(), std::logic_error);
  REQUIRE_THROWS_AS(encode(writer, value), std::logic_error);
  REQUIRE(sink.bytes_.size() == 1U);
}

}// namespace

TEST_CASE("integer coding encoders propagate output sink failures", "[integer_coding]")
{
  check_sink_failure(gamma_write, maximum);
  check_sink_failure(delta_write, maximum);
  check_sink_failure(rice_write, small_value_limit);
  check_sink_failure(golomb_write, small_value_limit);
}
