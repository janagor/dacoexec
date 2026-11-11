#include "error_test_utils.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/detail/arithmetic/archive_format.hpp>
#include <dacoexec/detail/arithmetic/binary_arithmetic_codec.hpp>
#include <dacoexec/views.hpp>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace impl = dacoexec::detail::arithmetic;
namespace api = dacoexec::arithmetic;
using bytes = std::vector<std::byte>;
using dacoexec::test::error_is;
constexpr auto binary = api::entropy_coder::binary;
}// namespace

TEST_CASE("Binary DAC1 golden archive freezes model framing and termination", "[arithmetic][binary][archive]")
{
  // Independent arbitrary-width Python interval calculation, struct.pack LE
  // fields, and zlib.crc32(bytes(64)). Smoothed counts 513:1 normalize to 4087:9
  // after reserving one frequency per outcome and apportioning 4094 slots.
  std::array const golden{ 0x44,
    0x41,
    0x43,
    0x31,
    0,
    0,
    0,
    0,
    3,
    64,
    0,
    0,
    0,
    5,
    0,
    0,
    0,
    0x36,
    0x63,
    0x8d,
    0x75,
    0xf6,
    0x0f,
    8,
    0,
    0x20,
    0,
    0,
    0,
    0,
    0xff,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0 };
  bytes expected;
  for (auto value : golden) { expected.push_back(static_cast<std::byte>(value)); }
  bytes const source(64, std::byte{ 0 });
  CHECK(api::compress(source, { .coder_ = binary }) == expected);
  CHECK(api::decompress(expected) == source);
}

TEST_CASE("Binary block adapter preserves byte order and extreme probabilities", "[arithmetic][binary]")
{
  for (auto probabilities : { impl::binary_model{ 1, 1 },
         impl::binary_model{ 1, 65535 },
         impl::binary_model{ 65535, 1 },
         impl::binary_model{ 3, 5 } }) {
    auto const metadata = impl::serialize_binary_model(probabilities);
    auto const restored = impl::deserialize_binary_model(metadata);
    CHECK(restored.zero_frequency_ == probabilities.zero_frequency_);
    CHECK(restored.one_frequency_ == probabilities.one_frequency_);
    for (unsigned size : { 0U, 1U, 257U, 4096U }) {
      bytes input;
      for (unsigned i = 0; i < size; ++i) { input.push_back(static_cast<std::byte>(i % 256)); }
      auto const payload = impl::encode_binary_block(input, probabilities);
      CHECK(impl::decode_binary_block(payload, probabilities, input.size(), input.size()) == input);
      auto trailing = payload;
      trailing.push_back(std::byte{ 0 });
      CHECK_THROWS_MATCHES(impl::decode_binary_block(trailing, probabilities, size, size),
        dacoexec::exception,
        error_is(dacoexec::errc::invalid_payload));
      if (size != 0) {
        CHECK_THROWS_MATCHES(impl::decode_binary_block(payload, probabilities, size, size - 1),
          dacoexec::exception,
          error_is(dacoexec::errc::output_limit_exceeded));
      }
    }
  }
  bytes const source{ std::byte{ 0x69 } };
  CHECK(impl::encode_binary_block(source, { 1, 1 })
        == bytes{ std::byte{ 0x69 }, std::byte{ 0x40 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
  std::istringstream input("single pass adapter");
  auto const payload = impl::encode_binary_block(
    std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()), { 3, 5 });
  std::string const expected = "single pass adapter";
  auto const output = impl::decode_binary_block(payload, { 3, 5 }, expected.size(), expected.size());
  CHECK(std::ranges::equal(output, expected | std::views::transform([](char c) { return static_cast<std::byte>(c); })));
}

TEST_CASE("Binary archives reset models per frame and include metadata in statistics", "[arithmetic][binary][archive]")
{
  bytes const block(1024, std::byte{ 0 });
  auto const one = api::compress(block, { .coder_ = binary });
  REQUIRE(one.at(8) == std::byte{ 3 });
  auto doubled = block;
  doubled.insert(doubled.end(), block.begin(), block.end());
  auto const two = api::compress(doubled, { .block_size_ = block.size(), .coder_ = binary });
  auto const frame_size = one.size() - 8 - 13;
  CHECK(std::ranges::equal(std::span(two).subspan(8, frame_size), std::span(two).subspan(8 + frame_size, frame_size)));
  auto frames = impl::stream_format::frames(two, block.size());
  auto const &frame = *frames.begin();
  CHECK_FALSE(frame.block_.model_.has_value());
  REQUIRE(frame.block_.binary_model_.has_value());
  CHECK(impl::model_metadata_size(frame.block_) == 4);

  bytes viewed;
  auto encoded = doubled | dacoexec::views::encode::arithmetic({ .block_size_ = block.size(), .coder_ = binary });
  std::ranges::copy(encoded, std::back_inserter(viewed));
  CHECK(viewed == two);
  bytes decoded;
  std::ranges::copy(viewed | dacoexec::views::decode::arithmetic(), std::back_inserter(decoded));
  CHECK(decoded == doubled);

  std::istringstream input(std::string(2048, '\0'));
  auto single_pass = std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  bytes wire;
  auto const stats =
    api::compress_stream(single_pass, std::back_inserter(wire), { .block_size_ = 1024, .coder_ = binary });
  CHECK(wire == two);
  CHECK(stats.frames_ == 2);
  CHECK(stats.input_bytes_ == doubled.size());
  CHECK(stats.output_bytes_ == wire.size());
  bytes output;
  auto const decoded_stats = api::decompress_stream(wire, std::back_inserter(output));
  CHECK(output == doubled);
  CHECK(decoded_stats.frames_ == 2);
  CHECK(decoded_stats.input_bytes_ == wire.size());
  CHECK(decoded_stats.output_bytes_ == doubled.size());
}

TEST_CASE("Binary archives fall back to RAW and decode mixed variants", "[arithmetic][binary][archive]")
{
  for (auto size : { 0U, 1U, 8U, 256U, 4096U }) {
    bytes source;
    for (unsigned i = 0; i < size; ++i) { source.push_back(static_cast<std::byte>(i % 256)); }
    auto const archive = api::compress(source, { .coder_ = binary });
    CHECK(api::decompress(archive) == source);
    if (size != 0) { CHECK(archive.at(8) == std::byte{ 0 }); }
  }
  std::mt19937 random{ 87 };
  for (unsigned pattern : { 0U, 1U, 2U }) {
    bytes source(10000, std::byte{ 0 });
    for (auto &value : source) {
      auto const word = random();
      value = static_cast<std::byte>(pattern == 0 ? word % 256 : (pattern == 1 ? word % 4 : word % 2));
    }
    auto const archive = api::compress(source, { .block_size_ = 1024, .coder_ = binary });
    CHECK(api::decompress(archive) == source);
    CHECK(api::compress(source, { .block_size_ = 1024, .coder_ = binary }) == archive);
  }
  bytes wire;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(wire) };
  impl::write_archive_header(sink);
  bytes expected;
  for (auto coder : { api::entropy_coder::static_range, api::entropy_coder::adaptive_range, binary }) {
    bytes const source(1024, std::byte{ 0xff });
    impl::write_frame(sink, impl::encode_frame({ 0, source }, coder));
    expected.insert(expected.end(), source.begin(), source.end());
  }
  impl::write_frame(sink, impl::encode_frame({ 0, bytes{ std::byte{ 0x69 } } }, binary));
  expected.push_back(std::byte{ 0x69 });
  impl::write_frame_header(sink, {});
  CHECK(api::decompress(wire) == expected);
}

TEST_CASE("Binary archives reject invalid models and noncanonical streams before output",
  "[arithmetic][binary][archive]")
{
  auto const archive = api::compress(bytes(64, std::byte{ 0 }), { .coder_ = binary });
  for (std::size_t offset = 25; offset < archive.size() - 13; ++offset) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      auto corrupt = archive;
      corrupt[offset] ^= static_cast<std::byte>(1U << bit);
      bytes output;
      CHECK_THROWS_AS(api::decompress_stream(corrupt, std::back_inserter(output)), dacoexec::exception);
      CHECK(output.empty());
    }
  }
  for (std::size_t size = 0; size < archive.size(); ++size) {
    CHECK_THROWS_AS(api::decompress(std::span(archive).first(size)), dacoexec::exception);
  }
  auto invalid_model = archive;
  std::fill_n(invalid_model.begin() + 21, 4, std::byte{ 0xff });
  CHECK_THROWS_MATCHES(api::decompress(invalid_model), dacoexec::exception, error_is(dacoexec::errc::invalid_frame));
  auto short_payload = archive;
  short_payload.at(13) = std::byte{ 3 };
  CHECK_THROWS_MATCHES(api::decompress(short_payload), dacoexec::exception, error_is(dacoexec::errc::invalid_frame));
  auto trailing_payload = archive;
  trailing_payload.insert(trailing_payload.end() - 13, std::byte{ 0 });
  trailing_payload.at(13) = std::byte{ 6 };
  CHECK_THROWS_MATCHES(
    api::decompress(trailing_payload), dacoexec::exception, error_is(dacoexec::errc::invalid_payload));
  CHECK_THROWS_MATCHES(api::decompress(archive, { .max_output_bytes_ = 63 }),
    dacoexec::exception,
    error_is(dacoexec::errc::output_limit_exceeded));
  CHECK_THROWS_MATCHES(api::decompress(archive, { .max_frame_size_ = 63 }),
    dacoexec::exception,
    error_is(dacoexec::errc::output_limit_exceeded));
  // A valid terminal stream can still represent different bytes: CRC is required.
  auto bad_crc = archive;
  bad_crc.at(17) ^= std::byte{ 1 };
  bytes output;
  CHECK_THROWS_MATCHES(api::decompress_stream(bad_crc, std::back_inserter(output)),
    dacoexec::exception,
    error_is(dacoexec::errc::checksum_mismatch));
  CHECK(output.empty());
}

TEST_CASE("Binary archives reject valid but incorrect model metadata before output", "[arithmetic][binary][archive]")
{
  auto const archive = api::compress(bytes(64, std::byte{ 0 }), { .coder_ = binary });
  REQUIRE(archive.at(8) == std::byte{ 3 });
  for (auto probabilities : { impl::binary_model{ 9, 4087 }, impl::binary_model{ 2048, 2048 } }) {
    auto corrupt = archive;
    // Change only the probabilities, preserving a valid model and total 4096.
    auto const metadata = impl::serialize_binary_model(probabilities);
    REQUIRE(impl::deserialize_binary_model(metadata).total() == 4096);
    std::ranges::copy(metadata, corrupt.begin() + 21);
    bytes output;
    CHECK_THROWS_MATCHES(api::decompress_stream(corrupt, std::back_inserter(output)),
      dacoexec::exception,
      error_is(dacoexec::errc::invalid_payload) || error_is(dacoexec::errc::checksum_mismatch));
    CHECK(output.empty());
  }
}

TEST_CASE("Binary owned blocks and frame writers validate model ownership", "[arithmetic][binary][archive]")
{
  bytes const input(64, std::byte{ 0 });
  for (unsigned field : { 0U, 1U, 2U }) {
    auto frame = impl::encode_frame({ 0, input }, binary);
    if (field == 0) { frame.block_.binary_model_.reset(); }
    if (field == 1) { frame.block_.model_ = impl::make_static_model(input); }
    if (field == 2) { frame.block_.binary_model_->zero_frequency_ = 0; }
    CHECK_THROWS_AS(impl::decoder{}.decode(frame.block_, input.size()), dacoexec::exception);
    bytes wire;
    dacoexec::detail::io::iterator_sink sink{ std::back_inserter(wire) };
    CHECK_THROWS_AS(impl::write_frame(sink, frame), dacoexec::exception);
    CHECK(wire.empty());
  }
  auto raw = impl::encoder{}.encode({ 0, bytes{ std::byte{ 1 } } });
  raw.binary_model_ = impl::binary_model{};
  CHECK_THROWS_MATCHES(impl::decoder{}.decode(raw, 1), dacoexec::exception, error_is(dacoexec::errc::invalid_payload));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
