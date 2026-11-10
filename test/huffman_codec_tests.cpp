#include "error_test_utils.hpp"
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/detail/huffman/encode_cost.hpp>
#include <dacoexec/detail/huffman/encoded_block.hpp>
#include <dacoexec/detail/huffman/tree.hpp>
#include <dacoexec/detail/huffman/types.hpp>
#include <dacoexec/detail/model/frequency.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <random>
#include <string_view>

namespace {
using dacoexec::detail::model::alphabet_size;
using dacoexec::detail::huffman::block_encoding;
using dacoexec::detail::huffman::build_code_lengths;
using dacoexec::detail::huffman::code_length_table;
using dacoexec::detail::huffman::compress_block;
using dacoexec::detail::model::count_frequencies;
using dacoexec::exception;
using dacoexec::detail::huffman::decompress_block;
using dacoexec::detail::huffman::encoded_block;
using dacoexec::detail::model::frequency_table;
using dacoexec::detail::huffman::max_canonical_bits;
namespace huffman_detail = dacoexec::detail::huffman;
constexpr std::size_t large_block = 4096;
constexpr std::size_t mebibyte = std::size_t{ 1024 } * 1024U;
constexpr std::uint64_t sequence = 42;
constexpr unsigned random_seed = 12345;
constexpr unsigned high_bit_value = 0x80;
constexpr unsigned all_bits_value = 0xFF;
constexpr unsigned golden_first = 0x05;
constexpr unsigned golden_second = 0x6E;
constexpr std::string_view sample = "AAAAABBCD";

[[nodiscard]] auto golden() -> encoded_block
{
  encoded_block block;
  block.sequence_ = sequence;
  block.encoding_ = block_encoding::canonical_huffman;
  block.uncompressed_size_ = sample.size();
  block.payload_ = { std::byte{ golden_first }, std::byte{ golden_second } };
  block.lengths_ = build_code_lengths(count_frequencies(sample));
  block.padding_bits_ = 1;
  return block;
}

auto round_trip(std::vector<std::byte> const &bytes, std::uint64_t seq = sequence) -> void
{
  auto encoded = compress_block({ seq, bytes });
  auto decoded = decompress_block(std::move(encoded), bytes.size());
  CHECK(decoded.sequence_ == seq);
  CHECK(decoded.bytes_ == bytes);
}
}// namespace

TEST_CASE("Block codec round trips boundary sizes and binary input", "[huffman][codec]")
{
  constexpr std::array sizes{ std::size_t{ 0 },
    std::size_t{ 1 },
    std::size_t{ 7 },
    std::size_t{ 8 },
    std::size_t{ 9 },
    alphabet_size - 1U,
    alphabet_size,
    alphabet_size + 1U,
    large_block,
    mebibyte };
  for (auto size : sizes) {
    round_trip(std::vector<std::byte>(size, std::byte{ 'A' }));
    std::vector<std::byte> binary(size);
    for (std::size_t index = 0; index < size; ++index) {
      binary.at(index) = static_cast<std::byte>(index % alphabet_size);
    }
    round_trip(binary);
  }
  round_trip({}, 0);
  round_trip({ std::byte{ 0 }, std::byte{ high_bit_value }, std::byte{ all_bits_value } },
    std::numeric_limits<std::uint64_t>::max());
  std::vector<std::byte> text;
  for (auto value : sample) { text.push_back(static_cast<std::byte>(value)); }
  round_trip(text);
  round_trip(std::vector<std::byte>{ std::byte{ 'A' }, std::byte{ 'B' } });
}

// Catch assertions expand into branches; keep the related selection checks together.
TEST_CASE("Block selection is conservative and deterministic", "[huffman][codec]")
{
  constexpr auto singleton_savings_bits = dacoexec::detail::support::bits_per_byte - 1U;
  constexpr auto tie_size = alphabet_size + (alphabet_size / singleton_savings_bits) + 1U;
  auto tie = compress_block({ sequence, std::vector<std::byte>(tie_size, std::byte{ 'A' }) });
  CHECK(tie.encoding_ == block_encoding::raw);
  auto small = compress_block({ sequence, { std::byte{ 'A' } } });
  CHECK(small.encoding_ == block_encoding::raw);
  std::vector<std::byte> uniform(alphabet_size);
  for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
    uniform.at(symbol) = static_cast<std::byte>(symbol);
  }
  auto const *original_storage = uniform.data();
  auto raw = compress_block({ sequence, std::move(uniform) });
  CHECK(raw.encoding_ == block_encoding::raw);
  CHECK(raw.payload_.data() == original_storage);
  auto original = decompress_block(std::move(raw), alphabet_size);
  CHECK(original.bytes_.data() == original_storage);
  auto first = compress_block({ sequence, std::vector<std::byte>(large_block, std::byte{ 'A' }) });
  auto second = compress_block({ sequence, std::vector<std::byte>(large_block, std::byte{ 'A' }) });
  REQUIRE(first.encoding_ == block_encoding::canonical_huffman);
  CHECK(first.lengths_ == second.lengths_);
  CHECK(first.payload_ == second.payload_);
  CHECK(first.padding_bits_ == second.padding_bits_);
  std::vector<std::byte> skewed(large_block, std::byte{ 'A' });
  skewed.back() = std::byte{ 'B' };
  CHECK(compress_block({ sequence, skewed }).encoding_ == block_encoding::canonical_huffman);
  frequency_table frequencies{};
  code_length_table lengths{};
  lengths.front() = max_canonical_bits + 1U;
  CHECK_FALSE(huffman_detail::encoded_bit_count(frequencies, lengths).has_value());
  lengths.front() = 1;
  frequencies.front() = std::numeric_limits<std::uint64_t>::max();
  lengths.at(1) = 1;
  frequencies.at(1) = 1;
  CHECK_FALSE(huffman_detail::encoded_bit_count(frequencies, lengths).has_value());
}

TEST_CASE("Independent canonical golden block decodes", "[huffman][codec]")
{
  auto decoded = decompress_block(golden(), sample.size());
  REQUIRE(decoded.bytes_.size() == sample.size());
  for (std::size_t index = 0; index < sample.size(); ++index) {
    CHECK(decoded.bytes_.at(index) == static_cast<std::byte>(sample.at(index)));
  }
}

TEST_CASE("Malformed canonical length tables fail at decoder boundary", "[huffman][codec]")
{
  auto block = golden();
  block.lengths_ = {};
  SECTION("oversubscribed")
  {
    block.lengths_.at(0) = 1;
    block.lengths_.at(1) = 1;
    block.lengths_.at(2) = 1;
  }
  SECTION("incomplete")
  {
    block.lengths_.at(0) = 2;
    block.lengths_.at(1) = 2;
  }
  SECTION("unsupported width") { block.lengths_.at(0) = max_canonical_bits + 1U; }
  SECTION("empty") {}
  CHECK_THROWS_MATCHES(decompress_block(std::move(block), sample.size()),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
}

TEST_CASE("Malformed Huffman payloads never return partial output", "[huffman][codec]")
{
  auto block = golden();
  SECTION("truncated symbol") { ++block.padding_bits_; }
  SECTION("truncated payload") { block.payload_.pop_back(); }
  SECTION("output too short") { --block.uncompressed_size_; }
  SECTION("output too long") { ++block.uncompressed_size_; }
  SECTION("nonzero padding") { block.payload_.back() |= std::byte{ 1 }; }
  SECTION("invalid padding count") { block.padding_bits_ = dacoexec::detail::support::bits_per_byte; }
  SECTION("extra complete codeword") { block.padding_bits_ = 0; }
  SECTION("extra payload byte") { block.payload_.push_back(std::byte{ 0 }); }
  SECTION("invalid singleton bit")
  {
    block.lengths_ = {};
    block.lengths_.front() = 1;
    block.payload_.front() = std::byte{ high_bit_value };
  }
  CHECK_THROWS_MATCHES(decompress_block(std::move(block), large_block),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
}

TEST_CASE("Block metadata and output limits are validated", "[huffman][codec]")
{
  CHECK_THROWS_MATCHES(decompress_block(golden(), 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  auto expected_code = dacoexec::errc::invalid_payload;
  encoded_block block;
  SECTION("raw size mismatch") { block.uncompressed_size_ = 1; }
  SECTION("raw padding") { block.padding_bits_ = 1; }
  SECTION("raw lengths") { block.lengths_.front() = 1; }
  // Deliberately exercise rejection of an unrecognized fixed-underlying-type value.
  SECTION("unknown encoding")
  {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    block.encoding_ = static_cast<block_encoding>(std::numeric_limits<std::uint8_t>::max());
  }
  SECTION("huge output")
  {
    block.uncompressed_size_ = std::numeric_limits<std::uint64_t>::max();
    expected_code = dacoexec::errc::output_limit_exceeded;
  }
  SECTION("empty Huffman") { block.encoding_ = block_encoding::canonical_huffman; }
  CHECK_THROWS_MATCHES(
    decompress_block(std::move(block), large_block), dacoexec::exception, dacoexec::test::error_is(expected_code));
}

TEST_CASE("Fixed seed random and skewed blocks round trip", "[huffman][codec]")
{
  // Fixed seed intentionally makes round-trip failures reproducible.
  std::mt19937 generator(random_seed);
  constexpr unsigned cases = 64;
  for (unsigned iteration = 0; iteration < cases; ++iteration) {
    std::vector<std::byte> bytes(generator() % large_block);
    for (auto &value : bytes) { value = static_cast<std::byte>(generator() % alphabet_size); }
    round_trip(bytes);
    for (auto &value : bytes) {
      if (generator() % alphabet_size != 0) { value = std::byte{ 'A' }; }
    }
    round_trip(bytes);
  }
}

TEST_CASE("Block decoder accepts maximum-width codewords", "[huffman][codec]")
{
  encoded_block block;
  block.encoding_ = block_encoding::canonical_huffman;
  block.uncompressed_size_ = 1;
  for (unsigned length = 1; length < max_canonical_bits; ++length) {
    block.lengths_.at(length - 1U) = static_cast<std::uint8_t>(length);
  }
  block.lengths_.at(max_canonical_bits - 1U) = max_canonical_bits;
  block.lengths_.at(max_canonical_bits) = max_canonical_bits;
  constexpr auto payload_size = max_canonical_bits / dacoexec::detail::support::bits_per_byte;
  block.payload_.assign(payload_size, static_cast<std::byte>(std::numeric_limits<std::uint8_t>::max()));
  auto decoded = decompress_block(std::move(block), 1);
  CHECK(decoded.bytes_ == std::vector<std::byte>{ static_cast<std::byte>(max_canonical_bits) });
}

TEST_CASE("Compressible full alphabet uses the Huffman path", "[huffman][codec]")
{
  std::vector<std::byte> bytes(large_block, std::byte{ 'A' });
  for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) { bytes.push_back(static_cast<std::byte>(symbol)); }
  auto encoded = compress_block({ sequence, bytes });
  REQUIRE(encoded.encoding_ == block_encoding::canonical_huffman);
  for (auto const length : encoded.lengths_) { CHECK(length != 0); }
  auto decoded = decompress_block(std::move(encoded), bytes.size());
  CHECK(decoded.sequence_ == sequence);
  CHECK(decoded.bytes_ == bytes);
}

TEST_CASE("Singleton compression matches fixed canonical output", "[huffman][codec]")
{
  static_assert(large_block % dacoexec::detail::support::bits_per_byte == 0);
  auto encoded = compress_block({ sequence, std::vector<std::byte>(large_block, std::byte{ 'A' }) });
  REQUIRE(encoded.encoding_ == block_encoding::canonical_huffman);
  code_length_table expected_lengths{};
  expected_lengths.at(static_cast<unsigned char>('A')) = 1;
  std::vector<std::byte> const expected_payload(large_block / dacoexec::detail::support::bits_per_byte, std::byte{ 0 });
  CHECK(encoded.sequence_ == sequence);
  CHECK(encoded.uncompressed_size_ == large_block);
  CHECK(encoded.lengths_ == expected_lengths);
  CHECK(encoded.payload_ == expected_payload);
  CHECK(encoded.padding_bits_ == 0);
}

TEST_CASE("Impossible Huffman output size fails the bit-count preflight", "[huffman][codec]")
{
  auto block = golden();
  constexpr std::size_t gibibyte = mebibyte * 1024U;
  block.uncompressed_size_ = gibibyte;
  // Check the specific preflight diagnostic, rather than accepting an error
  // raised only after reserving and starting to decode the tiny payload.
  try {
    static_cast<void>(decompress_block(std::move(block), gibibyte));
    FAIL("Impossible output size was accepted");
  } catch (dacoexec::exception const &error) {
    CHECK(error.code() == dacoexec::errc::invalid_payload);
    CHECK(std::string_view{ error.what() }.starts_with("Huffman payload cannot produce declared output size"));
  }
}
