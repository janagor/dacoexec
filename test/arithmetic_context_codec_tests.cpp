#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/detail/arithmetic/binary/context/codec.hpp>
#include <dacoexec/views.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace impl = dacoexec::detail::arithmetic;
namespace api = dacoexec::arithmetic;
using bytes = std::vector<std::byte>;
using dacoexec::test::error_is;
constexpr auto context_binary = api::entropy_coder::context_binary;
}// namespace

TEST_CASE("Context binary DAC1 golden archive freezes metadata-free framing", "[arithmetic][context][archive]")
{
  // Independent Python integer interval reference, LE struct.pack, and zlib CRC32.
  std::array const golden{ 0x44,
    0x41,
    0x43,
    0x31,
    0x00,
    0x00,
    0x00,
    0x00,
    0x04,
    0x40,
    0x00,
    0x00,
    0x00,
    0x0a,
    0x00,
    0x00,
    0x00,
    0x36,
    0x63,
    0x8d,
    0x75,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
    0x00,
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
  bytes const source(64, std::byte{ 0 });
  CHECK(api::compress(source, { .coder_ = context_binary }) == expected);
  CHECK(api::decompress(expected) == source);
}

TEST_CASE("Context binary mixed-bit golden archive freezes cross-byte history", "[arithmetic][context][archive]")
{
  // Independent Python unbounded integer interval reference with an explicit
  // 32-entry count table, rolling history, LE struct.pack, and zlib CRC32.
  std::array const golden{ 0x44,
    0x41,
    0x43,
    0x31,
    0x00,
    0x00,
    0x00,
    0x00,
    0x04,
    0x40,
    0x00,
    0x00,
    0x00,
    0x0f,
    0x00,
    0x00,
    0x00,
    0xdb,
    0xc4,
    0x55,
    0x2b,
    0x69,
    0x96,
    0x5b,
    0x69,
    0x62,
    0x71,
    0xc6,
    0x7f,
    0x50,
    0x13,
    0xe0,
    0x00,
    0x00,
    0x00,
    0x00,
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
  bytes source;
  for (unsigned index = 0; index < 32; ++index) {
    source.push_back(std::byte{ 0x69 });
    source.push_back(std::byte{ 0x96 });
  }
  CHECK(api::compress(source, { .coder_ = context_binary }) == expected);
  CHECK(api::decompress(expected) == source);
}

TEST_CASE("Context binary adapter synchronizes initial history and rescaling", "[arithmetic][context]")
{
  std::mt19937 random{ 1729 };
  for (unsigned pattern : { 0U, 1U, 2U, 3U }) {
    bytes source(80000, std::byte{ 0 });
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (pattern == 1) { source[i] = std::byte{ 0xff }; }
      if (pattern == 2) { source[i] = static_cast<std::byte>(i % 2 == 0 ? 0xaa : 0x55); }
      if (pattern == 3 || i >= 70000) { source[i] = static_cast<std::byte>(random() % 256); }
    }
    auto const archive = api::compress(source, { .coder_ = context_binary });
    CHECK(api::decompress(archive) == source);
    for (auto size : { 0U, 1U, 257U, 32767U, 32768U, 80000U }) {
      auto const input = std::span(source).first(size);
      auto const payload = impl::encode_context_binary_block(input);
      CHECK(impl::decode_context_binary_block(payload, size, size) == bytes(input.begin(), input.end()));
      CHECK(impl::encode_context_binary_block(input) == payload);
      if (size != 0) {
        CHECK_THROWS_MATCHES(impl::decode_context_binary_block(payload, size, size - 1),
          dacoexec::exception,
          error_is(dacoexec::errc::output_limit_exceeded));
      }
      auto trailing = payload;
      trailing.push_back(std::byte{ 0 });
      CHECK_THROWS_MATCHES(impl::decode_context_binary_block(trailing, size, size),
        dacoexec::exception,
        error_is(dacoexec::errc::invalid_payload));
    }
  }
  bytes const source{ std::byte{ 0x69 }, std::byte{ 0x69 } };
  CHECK(
    impl::encode_context_binary_block(source)
    == bytes{ std::byte{ 0x69 }, std::byte{ 0x62 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
  std::istringstream input("ii");
  CHECK(impl::encode_context_binary_block(
          std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()))
        == impl::encode_context_binary_block(source));
}

TEST_CASE("Context binary frames reset probabilities history and coding state", "[arithmetic][context][archive]")
{
  // Ending in 0xff makes leaked history observable in the next frame.
  bytes block(4096, std::byte{ 0x69 });
  block.back() = std::byte{ 0xff };
  auto doubled = block;
  doubled.insert(doubled.end(), block.begin(), block.end());
  auto const one = api::compress(block, { .coder_ = context_binary });
  auto const two = api::compress(doubled, { .block_size_ = block.size(), .coder_ = context_binary });
  REQUIRE(one.at(8) == std::byte{ 4 });
  auto const frame_size = one.size() - 8 - 13;
  CHECK(std::ranges::equal(std::span(one).subspan(8, frame_size), std::span(two).subspan(8, frame_size)));
  CHECK(std::ranges::equal(std::span(two).subspan(8, frame_size), std::span(two).subspan(8 + frame_size, frame_size)));
  CHECK(api::decompress(two) == doubled);
  auto frames = impl::stream_format::frames(two, block.size());
  auto const &frame = *frames.begin();
  CHECK_FALSE(frame.block_.model_);
  CHECK_FALSE(frame.block_.binary_model_);
  CHECK(impl::model_metadata_size(frame.block_) == 0);
  CHECK(frame_size == 13 + frame.block_.payload_.size());

  bytes viewed;
  std::ranges::copy(
    doubled | dacoexec::views::encode::arithmetic({ .block_size_ = block.size(), .coder_ = context_binary }),
    std::back_inserter(viewed));
  CHECK(viewed == two);
  bytes decoded;
  std::ranges::copy(viewed | dacoexec::views::decode::arithmetic(), std::back_inserter(decoded));
  CHECK(decoded == doubled);

  std::string text;
  for (auto value : doubled) { text.push_back(static_cast<char>(std::to_integer<unsigned char>(value))); }
  std::istringstream input(text);
  bytes wire;
  auto const stats =
    api::compress_stream(std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
      std::back_inserter(wire),
      { .block_size_ = block.size(), .coder_ = context_binary });
  CHECK(wire == two);
  CHECK(stats.frames_ == 2);
  CHECK(stats.output_bytes_ == wire.size());
  CHECK(stats.input_bytes_ == doubled.size());
  bytes output;
  auto const restored = api::decompress_stream(wire, std::back_inserter(output));
  CHECK(output == doubled);
  CHECK(restored.frames_ == 2);
  CHECK(restored.input_bytes_ == wire.size());
  CHECK(restored.output_bytes_ == doubled.size());
}

TEST_CASE("Context binary archives support RAW fallback and mixed variants", "[arithmetic][context][archive]")
{
  for (auto size : { 0U, 1U, 4U }) {
    bytes const input(size, std::byte{ 0x69 });
    auto const archive = api::compress(input, { .coder_ = context_binary });
    CHECK(api::decompress(archive) == input);
    if (size != 0) { CHECK(archive.at(8) == std::byte{ 0 }); }
  }
  std::mt19937 random{ 42 };
  bytes noise(4096);
  for (auto &value : noise) { value = static_cast<std::byte>(random() % 256); }
  auto const raw = api::compress(noise, { .coder_ = context_binary });
  CHECK(raw.at(8) == std::byte{ 0 });
  CHECK(api::decompress(raw) == noise);
  bytes wire;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(wire) };
  impl::write_archive_header(sink);
  bytes expected;
  std::array const variants{ std::pair{ api::entropy_coder::static_range, impl::frame_kind::static_range },
    std::pair{ api::entropy_coder::adaptive_range, impl::frame_kind::adaptive_range },
    std::pair{ api::entropy_coder::binary, impl::frame_kind::binary },
    std::pair{ context_binary, impl::frame_kind::context_binary } };
  for (auto const &[coder, expected_kind] : variants) {
    bytes const input(1024, std::byte{ 0xff });
    auto frame = impl::encode_frame({ 0, input }, coder);
    REQUIRE(frame.header_.kind_ == expected_kind);
    impl::write_frame(sink, frame);
    expected.insert(expected.end(), input.begin(), input.end());
  }
  impl::write_frame_header(sink, {});
  CHECK(api::decompress(wire) == expected);
}

TEST_CASE("Context binary archives reject corruption before publishing output", "[arithmetic][context][archive]")
{
  auto const archive = api::compress(bytes(64, std::byte{ 0 }), { .coder_ = context_binary });
  REQUIRE(archive.at(8) == std::byte{ 4 });
  // finish() requires the canonical terminal code and zero padding. Payload
  // flips therefore fail either canonical validation or the decoded-data CRC.
  // Focused tests below verify those mechanisms separately.
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
  auto short_payload = archive;
  short_payload.at(13) = std::byte{ 3 };
  CHECK_THROWS_MATCHES(api::decompress(short_payload), dacoexec::exception, error_is(dacoexec::errc::invalid_frame));
  CHECK_THROWS_MATCHES(api::decompress(archive, { .max_output_bytes_ = 63 }),
    dacoexec::exception,
    error_is(dacoexec::errc::output_limit_exceeded));
  CHECK_THROWS_MATCHES(api::decompress(archive, { .max_frame_size_ = 63 }),
    dacoexec::exception,
    error_is(dacoexec::errc::output_limit_exceeded));
  auto bad_crc = archive;
  bad_crc.at(17) ^= std::byte{ 1 };
  bytes output;
  CHECK_THROWS_MATCHES(api::decompress_stream(bad_crc, std::back_inserter(output)),
    dacoexec::exception,
    error_is(dacoexec::errc::checksum_mismatch));
  CHECK(output.empty());

  for (bool fixed_model : { false, true }) {
    auto frame = impl::encode_frame({ 0, bytes(64, std::byte{ 0 }) }, context_binary);
    if (fixed_model) {
      frame.block_.binary_model_ = impl::binary_model{};
    } else {
      frame.block_.model_ = impl::make_static_model(bytes(64, std::byte{ 0 }));
    }
    CHECK_THROWS_AS(impl::decoder{}.decode(frame.block_, 64), dacoexec::exception);
    bytes wire;
    dacoexec::detail::io::iterator_sink sink{ std::back_inserter(wire) };
    CHECK_THROWS_AS(impl::write_frame(sink, frame), dacoexec::exception);
    CHECK(wire.empty());
  }
}

TEST_CASE("Context binary validates termination padding trailing bytes and CRC separately",
  "[arithmetic][context][archive]")
{
  bytes const source{ std::byte{ 0x69 }, std::byte{ 0x69 } };
  auto const payload = impl::encode_context_binary_block(source);
  for (bool padding : { false, true }) {
    auto corrupt = payload;
    // The final byte has three unused padding bits for this golden payload.
    // The preceding byte is part of the terminal code's zero guard bits.
    corrupt.at(corrupt.size() - (padding ? 1U : 2U)) ^= std::byte{ 1 };
    impl::binary_arithmetic_decoder decoder(corrupt);
    impl::context_model model;
    for (auto expected : source) {
      unsigned byte = 0;
      for (unsigned position = 0; position < 8; ++position) {
        auto const bit = decoder.decode(model.probabilities());
        model.update(bit);
        byte = (byte << 1U) | static_cast<unsigned>(bit);
      }
      CHECK(static_cast<std::byte>(byte) == expected);
    }
    // Identical decoded bytes cannot detect these changes via CRC32.
    CHECK_THROWS_MATCHES(decoder.finish(), dacoexec::exception, error_is(dacoexec::errc::invalid_payload));
  }

  auto const archive = api::compress(bytes(64, std::byte{ 0 }), { .coder_ = context_binary });
  auto trailing = archive;
  trailing.insert(trailing.end() - 13, std::byte{ 0 });
  trailing.at(13) = static_cast<std::byte>(std::to_integer<unsigned>(trailing.at(13)) + 1U);
  bytes output;
  CHECK_THROWS_MATCHES(api::decompress_stream(trailing, std::back_inserter(output)),
    dacoexec::exception,
    error_is(dacoexec::errc::invalid_payload));
  CHECK(output.empty());

  // A different canonical payload passes termination, but the original
  // uncompressed-data CRC must prevent publication of the changed bytes.
  auto changed = api::compress(bytes(64, std::byte{ 0xff }), { .coder_ = context_binary });
  std::copy_n(archive.begin() + 17, 4, changed.begin() + 17);
  CHECK_THROWS_MATCHES(api::decompress_stream(changed, std::back_inserter(output)),
    dacoexec::exception,
    error_is(dacoexec::errc::checksum_mismatch));
  CHECK(output.empty());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
