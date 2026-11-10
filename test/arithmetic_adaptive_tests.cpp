#include "error_test_utils.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/detail/arithmetic/adaptive_range_codec.hpp>
#include <dacoexec/detail/arithmetic/archive_format.hpp>
#include <dacoexec/detail/arithmetic/fenwick_adaptive_model.hpp>
#include <dacoexec/views.hpp>
#include <iterator>
#include <sstream>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace impl = dacoexec::detail::arithmetic;
namespace api = dacoexec::arithmetic;
using bytes = std::vector<std::byte>;
using dacoexec::test::error_is;
constexpr auto adaptive = api::entropy_coder::adaptive_range;
}// namespace

TEST_CASE("Adaptive range golden payload freezes initial probabilities and updates", "[arithmetic][adaptive]")
{
  std::string const input = "ABRACADABRA";
  // Independent Python reference: unbounded low += (range / total) * cumulative;
  // shift low by eight on normalization, then serialize in shifts + 5 bytes.
  std::vector<unsigned> const golden{ 0, 65, 67, 16, 71, 200, 41, 27, 249, 222, 99, 133, 231, 152 };
  bytes expected;
  for (auto value : golden) { expected.push_back(static_cast<std::byte>(value)); }
  CHECK(impl::encode_adaptive_block(input) == expected);
  auto const decoded = impl::decode_adaptive_block(expected, input.size(), input.size());
  CHECK(std::ranges::equal(decoded, input | std::views::transform([](char c) { return static_cast<std::byte>(c); })));
}

TEST_CASE("Adaptive DAC1 complete golden archive freezes framing and CRC", "[arithmetic][adaptive]")
{
  // Independent Python arbitrary-width range calculation with uniform priors,
  // post-symbol updates, struct.pack('<BIII', ...), and zlib.crc32(b'A' * 64).
  std::vector<unsigned> const golden{ 0x44,
    0x41,
    0x43,
    0x31,
    0x00,
    0x00,
    0x00,
    0x00,
    0x02,
    0x40,
    0x00,
    0x00,
    0x00,
    0x21,
    0x00,
    0x00,
    0x00,
    0x3c,
    0x62,
    0x4c,
    0x41,
    0x00,
    0x41,
    0x41,
    0x40,
    0xff,
    0xc5,
    0xce,
    0x2c,
    0x96,
    0xf2,
    0x60,
    0x99,
    0xff,
    0x4b,
    0x7f,
    0x73,
    0xd6,
    0xa9,
    0xf3,
    0x78,
    0xf4,
    0x87,
    0x72,
    0x88,
    0x4d,
    0x27,
    0xe2,
    0x1b,
    0xc3,
    0x1a,
    0xc3,
    0x3c,
    0xfb,
    0xff,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00 };
  bytes expected;
  for (auto value : golden) { expected.push_back(static_cast<std::byte>(value)); }
  bytes const source(64, std::byte{ 'A' });
  CHECK(api::compress(source, { .coder_ = adaptive }) == expected);
  CHECK(api::decompress(expected) == source);
}

TEST_CASE("Adaptive model representations share range bytes across rescaling", "[arithmetic][adaptive]")
{
  for (auto pattern : { 0U, 1U, 2U }) {
    bytes source(100000, std::byte{ 0 });
    for (std::size_t index = 0; index < source.size(); ++index) {
      if (pattern == 1) { source[index] = static_cast<std::byte>(index % 256); }
      if (pattern == 2) { source[index] = static_cast<std::byte>((index / 8192) % 256); }
    }
    auto const array_payload = impl::encode_adaptive_block(source);
    auto const fenwick_payload = impl::encode_adaptive_block<impl::fenwick_adaptive_model>(source);
    CHECK(array_payload == fenwick_payload);
    CHECK(
      impl::decode_adaptive_block<impl::fenwick_adaptive_model>(array_payload, source.size(), source.size()) == source);
    CHECK(impl::decode_adaptive_block(fenwick_payload, source.size(), source.size()) == source);
  }
}

TEST_CASE("Adaptive range synchronizes unseen symbols and rescaling boundaries", "[arithmetic][adaptive]")
{
  for (auto size : { 0U, 1U, 32512U, 32513U, 32514U, 100000U }) {
    CAPTURE(size);
    bytes input(size, std::byte{ 0 });
    // Introduce all previously unseen symbols immediately after the first rescale.
    for (std::size_t index = 32513; index < input.size(); ++index) {
      input[index] = static_cast<std::byte>(index % 256);
    }
    auto const payload = impl::encode_adaptive_block(input);
    CHECK(impl::decode_adaptive_block(payload, input.size(), input.size()) == input);
    CHECK(impl::encode_adaptive_block(input) == payload);
  }
  std::istringstream source("new symbols: abcXYZ");
  auto const payload = impl::encode_adaptive_block(
    std::ranges::subrange(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()));
  std::string const expected = "new symbols: abcXYZ";
  auto const output = impl::decode_adaptive_block(payload, expected.size(), expected.size());
  CHECK(std::ranges::equal(output, expected | std::views::transform([](char c) { return static_cast<std::byte>(c); })));
}

TEST_CASE("Adaptive DAC1 uses tag two without metadata and resets per frame", "[arithmetic][adaptive]")
{
  bytes const block(4096, std::byte{ 'A' });
  auto const payload = impl::encode_adaptive_block(block);
  auto const archive = api::compress(block, { .coder_ = adaptive });
  REQUIRE(archive.at(8) == std::byte{ 2 });
  CHECK(archive.size() == 8 + 13 + payload.size() + 13);
  CHECK(std::ranges::equal(std::span(archive).subspan(21, payload.size()), payload));
  CHECK(api::decompress(archive) == block);
  auto frames = impl::stream_format::frames(archive, block.size());
  REQUIRE_FALSE((*frames.begin()).block_.model_.has_value());
  auto doubled = block;
  doubled.insert(doubled.end(), block.begin(), block.end());
  auto const two = api::compress(doubled, { .block_size_ = block.size(), .coder_ = adaptive });
  auto const frame_size = 13 + payload.size();
  CHECK(std::ranges::equal(std::span(two).subspan(8, frame_size), std::span(two).subspan(8 + frame_size, frame_size)));
  bytes viewed;
  auto encoded = doubled | dacoexec::views::encode::arithmetic({ .block_size_ = block.size(), .coder_ = adaptive });
  std::ranges::copy(encoded, std::back_inserter(viewed));
  CHECK(viewed == two);
  bytes decoded;
  std::ranges::copy(viewed | dacoexec::views::decode::arithmetic(), std::back_inserter(decoded));
  CHECK(decoded == doubled);
  for (auto size : { 0U, 1U, 5U, 256U }) {
    bytes input;
    for (unsigned i = 0; i < size; ++i) { input.push_back(static_cast<std::byte>(i)); }
    auto const raw = api::compress(input, { .coder_ = adaptive });
    CHECK(api::decompress(raw) == input);
    if (size != 0) { CHECK(raw.at(8) == std::byte{ 0 }); }
  }
}

TEST_CASE("DAC1 decodes mixed static adaptive and raw frames in one stream", "[arithmetic][adaptive]")
{
  bytes wire;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(wire) };
  impl::write_archive_header(sink);
  bytes const source(1024, std::byte{ 'C' });
  impl::write_frame(sink, impl::encode_frame({ 0, source }));
  impl::write_frame(sink, impl::encode_frame({ 1, source }, adaptive));
  impl::write_frame(sink, impl::encode_frame({ 2, bytes{ std::byte{ 255 } } }, adaptive));
  impl::write_frame_header(sink, {});
  auto expected = source;
  expected.insert(expected.end(), source.begin(), source.end());
  expected.push_back(std::byte{ 255 });
  bytes output;
  auto const stats = api::decompress_stream(wire, std::back_inserter(output));
  CHECK(output == expected);
  CHECK(stats.frames_ == 3);
  CHECK(stats.input_bytes_ == wire.size());
  CHECK(stats.output_bytes_ == expected.size());
  std::istringstream input(std::string(4096, 'D'));
  auto single_pass = std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  bytes encoded;
  auto const encoding =
    api::compress_stream(single_pass, std::back_inserter(encoded), { .block_size_ = 1024, .coder_ = adaptive });
  CHECK(encoding.frames_ == 4);
  CHECK(encoding.input_bytes_ == 4096);
  CHECK(encoding.output_bytes_ == encoded.size());
  CHECK(encoded == api::compress(bytes(4096, std::byte{ 'D' }), { .block_size_ = 1024, .coder_ = adaptive }));
  CHECK(api::decompress(encoded) == bytes(4096, std::byte{ 'D' }));
}

TEST_CASE("Adaptive DAC1 rejects corruption before committing a frame", "[arithmetic][adaptive]")
{
  bytes const input(1024, std::byte{ 'B' });
  auto const archive = api::compress(input, { .coder_ = adaptive });
  for (std::size_t offset = 21; offset < archive.size() - 13; ++offset) {
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
  CHECK_THROWS_MATCHES(api::decompress(archive, { .max_output_bytes_ = 1023 }),
    dacoexec::exception,
    error_is(dacoexec::errc::output_limit_exceeded));
  CHECK_THROWS_MATCHES(
    impl::decode_adaptive_block({}, 1024, 1023), dacoexec::exception, error_is(dacoexec::errc::output_limit_exceeded));
  auto frame = impl::encode_frame({ 0, input }, adaptive);
  frame.block_.model_ = impl::make_static_model(input);
  CHECK_THROWS_AS(impl::decoder{}.decode(frame.block_, input.size()), dacoexec::exception);
  std::vector<std::byte> wire;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(wire) };
  CHECK_THROWS_AS(impl::write_frame(sink, frame), dacoexec::exception);
  CHECK(wire.empty());
  CHECK_THROWS_AS(impl::validate_frame_header({ impl::frame_kind::adaptive_range, 1024, 4, 0 }), dacoexec::exception);
}

TEST_CASE("Adaptive estimation improves long changing distributions", "[arithmetic][adaptive]")
{
  bytes input;
  for (unsigned symbol = 0; symbol < 8; ++symbol) { input.insert(input.end(), 65536, static_cast<std::byte>(symbol)); }
  auto const adaptive_archive = api::compress(input, { .coder_ = adaptive });
  auto const static_archive = api::compress(input);
  CHECK(adaptive_archive.size() < static_archive.size());
  CHECK(api::decompress(adaptive_archive) == input);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
