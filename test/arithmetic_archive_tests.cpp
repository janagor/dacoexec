#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/detail/ans/archive_format.hpp>
#include <dacoexec/detail/arithmetic/archive_format.hpp>
#include <dacoexec/detail/codec/stream.hpp>

#include <array>
#include <iterator>
#include <random>
#include <sstream>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
namespace codec = dacoexec::detail::codec;
using dacoexec::errc;
using dacoexec::exception;
using dacoexec::test::error_is;
using bytes = std::vector<std::byte>;

struct recording_sink
{
  bytes bytes_;

  void write(std::span<std::byte const> input) { bytes_.insert(bytes_.end(), input.begin(), input.end()); }
};

auto compress(bytes const &source, arithmetic::stream_options options = {}) -> bytes
{
  recording_sink sink;
  auto const stats = codec::compress_stream<arithmetic::stream_format>(source, sink, options);
  REQUIRE(stats.input_bytes_ == source.size());
  REQUIRE(stats.output_bytes_ == sink.bytes_.size());
  return sink.bytes_;
}

auto decompress(bytes const &archive, arithmetic::decode_options options = {}) -> bytes
{
  recording_sink sink;
  auto const stats = codec::decompress_stream<arithmetic::stream_format>(archive, sink, options);
  REQUIRE(stats.input_bytes_ == archive.size());
  REQUIRE(stats.output_bytes_ == sink.bytes_.size());
  return sink.bytes_;
}
}// namespace

TEST_CASE("DAC1 block fallback includes model cost and preserves identity", "[arithmetic][archive]")
{
  for (auto size : { 0U, 1U, 6U, 10U, 11U, 12U, 4096U }) {
    bytes source(size, std::byte{ 'A' });
    auto block = arithmetic::compress_block({ 42, source });
    REQUIRE(block.sequence_ == 42);
    REQUIRE(block.uncompressed_size_ == size);
    // One-symbol model costs six bytes and the payload costs five; ties use RAW.
    REQUIRE(
      block.encoding_ == (size <= 11 ? arithmetic::block_encoding::raw : arithmetic::block_encoding::static_range));
    REQUIRE(block.model_.has_value() == (size > 11));
    auto decoded = arithmetic::decompress_block(std::move(block), size);
    REQUIRE(decoded.sequence_ == 42);
    REQUIRE(decoded.bytes_ == source);
  }
  REQUIRE(arithmetic::encoder::minimum_compressed_size == 11);
  REQUIRE_FALSE(arithmetic::encoder{}.try_encode(bytes(11, std::byte{ 'A' })).has_value());
  bytes all_symbols;
  for (unsigned symbol = 0; symbol < 256; ++symbol) { all_symbols.push_back(static_cast<std::byte>(symbol)); }
  auto raw = arithmetic::compress_block({ 0, all_symbols });
  REQUIRE(raw.encoding_ == arithmetic::block_encoding::raw);
  REQUIRE(raw.payload_ == all_symbols);
}

TEST_CASE("DAC1 archive bytes freeze magic tags metadata and termination", "[arithmetic][archive]")
{
  auto empty = compress({});
  bytes expected{ std::byte{ 'D' },
    std::byte{ 'A' },
    std::byte{ 'C' },
    std::byte{ '1' },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 255 } };
  expected.resize(21);
  REQUIRE(empty == expected);
  REQUIRE(decompress(empty).empty());

  auto archive = compress(bytes(12, std::byte{ 'A' }));
  // Full fixture, including the independently computed CRC32 of twelve 'A' bytes.
  std::array<unsigned char, 45> const golden{ 0x44,
    0x41,
    0x43,
    0x31,
    0,
    0,
    0,
    0,
    1,
    12,
    0,
    0,
    0,
    5,
    0,
    0,
    0,
    0x66,
    0x43,
    0xd1,
    0x63,
    12,
    1,
    0,
    0x41,
    0xff,
    0x0f,
    0,
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
  bytes golden_bytes;
  for (auto byte : golden) { golden_bytes.push_back(static_cast<std::byte>(byte)); }
  REQUIRE(archive == golden_bytes);
  REQUIRE(decompress(golden_bytes) == bytes(12, std::byte{ 'A' }));
  auto frame = arithmetic::encode_frame({ 0, bytes(12, std::byte{ 'A' }) });
  REQUIRE(archive.at(8) == std::byte{ 1 });
  REQUIRE(archive.at(9) == std::byte{ 12 });
  REQUIRE(archive.at(13) == std::byte{ 5 });
  bytes const model{
    std::byte{ 12 }, std::byte{ 1 }, std::byte{ 0 }, std::byte{ 'A' }, std::byte{ 255 }, std::byte{ 15 }
  };
  REQUIRE(bytes(archive.begin() + 21, archive.begin() + 27) == model);
  REQUIRE(bytes(archive.begin() + 27, archive.begin() + 32) == bytes(5));
  REQUIRE(archive.at(32) == std::byte{ 255 });
  REQUIRE(archive.size() == 45);
  REQUIRE(decompress(archive) == bytes(12, std::byte{ 'A' }));
  REQUIRE(frame.header_.crc32_ == dacoexec::detail::support::crc32(bytes(12, std::byte{ 'A' })));
}

TEST_CASE("DAC1 mixed frames round trip deterministically through single pass input", "[arithmetic][archive]")
{
  bytes source(1024, std::byte{ 17 });
  std::mt19937 generator{ 123 };
  for (unsigned i = 0; i < 1024; ++i) { source.push_back(static_cast<std::byte>(generator() & 255U)); }
  source.insert(source.end(), 1024, std::byte{ 93 });
  auto archive = compress(source, { .block_size_ = 1024 });
  REQUIRE(compress(source, { .block_size_ = 1024 }) == archive);
  REQUIRE(decompress(archive) == source);
  auto frames = arithmetic::stream_format::frames(archive, 1024);
  auto cursor = frames.begin();
  REQUIRE((*cursor).header_.kind_ == arithmetic::frame_kind::static_range);
  ++cursor;
  REQUIRE((*cursor).header_.kind_ == arithmetic::frame_kind::raw);
  ++cursor;
  REQUIRE((*cursor).block_.sequence_ == 2);
  REQUIRE((*cursor).header_.kind_ == arithmetic::frame_kind::static_range);
  ++cursor;
  REQUIRE(cursor == frames.end());

  std::string wire;
  for (auto byte : archive) { wire.push_back(static_cast<char>(std::to_integer<unsigned char>(byte))); }
  std::istringstream input(wire);
  auto range = std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  recording_sink sink;
  auto stats = codec::decompress_stream<arithmetic::stream_format>(range, sink, {});
  REQUIRE(stats.frames_ == 3);
  REQUIRE(sink.bytes_ == source);
}

TEST_CASE("DAC1 multi-symbol models round trip across block boundaries", "[arithmetic][archive]")
{
  std::mt19937 generator{ 77 };
  for (auto alphabet : { 2U, 17U, 256U }) {
    bytes source;
    for (unsigned index = 0; index < 8193; ++index) {
      auto const symbol = index % 4 == 0 ? generator() % alphabet : 0;
      source.push_back(static_cast<std::byte>(symbol));
    }
    for (auto size : { 257U, 1024U, 8193U }) {
      auto const archive = compress(source, { .block_size_ = size });
      if (size == source.size()) { REQUIRE(archive.at(8) == std::byte{ 1 }); }
      REQUIRE(decompress(archive) == source);
    }
  }
}

TEST_CASE("DAC1 rejects corrupt framing models and payloads", "[arithmetic][archive]")
{
  auto const valid = compress(bytes(1024, std::byte{ 'A' }));
  for (std::size_t length = 0; length < valid.size(); ++length) {
    REQUIRE_THROWS_AS(decompress(bytes(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length))), exception);
  }
  auto broken = valid;
  broken.at(2) = std::byte{ 'N' };
  REQUIRE_THROWS_MATCHES(decompress(broken), exception, error_is(errc::invalid_archive));
  broken = valid;
  broken.push_back(std::byte{ 0 });
  REQUIRE_THROWS_MATCHES(decompress(broken), exception, error_is(errc::invalid_archive));
  for (auto tag : { 2, 3, 4, 5 }) {
    broken = valid;
    broken.at(8) = static_cast<std::byte>(tag);
    REQUIRE_THROWS_MATCHES(
      decompress(broken), exception, error_is(tag == 5 ? errc::invalid_frame : errc::unsupported_coder));
  }
  for (auto offset : { 21U, 22U, 25U }) {
    broken = valid;
    broken.at(offset) = std::byte{ 0 };
    REQUIRE_THROWS_MATCHES(decompress(broken), exception, error_is(errc::invalid_frame));
  }
  broken = valid;
  broken.at(27) = std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(decompress(broken), exception, error_is(errc::invalid_payload));
  broken = valid;
  broken.at(broken.size() - 12) = std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(decompress(broken), exception, error_is(errc::invalid_frame));

  for (auto archive : { valid, compress(bytes{ std::byte{ 7 } }) }) {
    archive.at(17) ^= std::byte{ 1 };
    recording_sink sink;
    REQUIRE_THROWS_MATCHES(codec::decompress_stream<arithmetic::stream_format>(archive, sink, {}),
      exception,
      error_is(errc::checksum_mismatch));
    REQUIRE(sink.bytes_.empty());
  }
}

TEST_CASE("DAC1 enforces output bounds and supported options before publication", "[arithmetic][archive]")
{
  auto const valid = compress(bytes(2048, std::byte{ 'A' }), { .block_size_ = 1024 });
  REQUIRE_THROWS_MATCHES(
    decompress(valid, { .max_frame_size_ = 1023 }), exception, error_is(errc::output_limit_exceeded));
  REQUIRE_THROWS_MATCHES(
    decompress(valid, { .max_output_bytes_ = 2047 }), exception, error_is(errc::output_limit_exceeded));
  REQUIRE_THROWS_MATCHES(
    decompress(valid, { .max_output_bytes_ = 0 }), exception, error_is(errc::output_limit_exceeded));
  REQUIRE(decompress(valid, { .max_frame_size_ = 1024, .max_output_bytes_ = 2048 }).size() == 2048);
  REQUIRE(decompress(compress({}), { .max_output_bytes_ = 0 }).empty());
  for (auto limit : { std::size_t{ 0 }, arithmetic::max_frame_size + 1 }) {
    REQUIRE_THROWS_MATCHES(
      decompress(valid, { .max_frame_size_ = limit }), exception, error_is(errc::invalid_argument));
    REQUIRE_THROWS_MATCHES(compress({}, { .block_size_ = limit }), exception, error_is(errc::invalid_argument));
  }
  for (auto coder : { arithmetic::entropy_coder::adaptive_range,
         arithmetic::entropy_coder::binary,
         arithmetic::entropy_coder::context_binary,
         static_cast<arithmetic::entropy_coder>(255) }) {
    recording_sink sink;
    REQUIRE_THROWS_MATCHES(codec::compress_stream<arithmetic::stream_format>(bytes{}, sink, { .coder_ = coder }),
      exception,
      error_is(errc::unsupported_coder));
    REQUIRE(sink.bytes_.empty());
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST_CASE("DAC1 validates owned block and writer invariants", "[arithmetic][archive]")
{
  auto raw = arithmetic::compress_block({ 0, bytes{ std::byte{ 7 } } });
  raw.uncompressed_size_ = 2;
  REQUIRE_THROWS_MATCHES(arithmetic::decompress_block(raw, 2), exception, error_is(errc::invalid_payload));
  REQUIRE_THROWS_MATCHES(arithmetic::decompress_block(raw, 1), exception, error_is(errc::output_limit_exceeded));
  raw.model_ = arithmetic::make_static_model(bytes{ std::byte{ 7 } });
  REQUIRE_THROWS_MATCHES(arithmetic::decompress_block(raw, 2), exception, error_is(errc::invalid_payload));
  auto compressed = arithmetic::compress_block({ 0, bytes(1024, std::byte{ 7 }) });
  compressed.model_.reset();
  REQUIRE_THROWS_MATCHES(arithmetic::decompress_block(compressed, 1024), exception, error_is(errc::invalid_payload));

  for (auto field : { 0, 1, 2, 3, 4 }) {
    auto frame = arithmetic::encode_frame({ 0, bytes(1024, std::byte{ 7 }) });
    switch (field) {
    case 0:
      ++frame.header_.payload_size_;
      break;
    case 1:
      ++frame.header_.uncompressed_size_;
      break;
    case 2:
      frame.block_.encoding_ = arithmetic::block_encoding::raw;
      break;
    case 3:
      frame.block_.model_.reset();
      break;
    case 4:
      frame.header_ = {};
      break;
    }
    recording_sink sink;
    REQUIRE_THROWS_MATCHES(arithmetic::write_frame(sink, frame), exception, error_is(errc::invalid_frame));
    REQUIRE(sink.bytes_.empty());
  }
  REQUIRE_THROWS_MATCHES(arithmetic::encode_frame({ 0, {} }), exception, error_is(errc::invalid_frame));
}

TEST_CASE("DAC1 rejects invalid sizes and unordered model entries", "[arithmetic][archive]")
{
  auto const valid = compress(bytes(1024, std::byte{ 'A' }));
  for (auto header : { arithmetic::frame_header{ arithmetic::frame_kind::raw, 2, 1, 0 },
         arithmetic::frame_header{ arithmetic::frame_kind::raw, 0, 0, 0 },
         arithmetic::frame_header{ arithmetic::frame_kind::raw, 16777217, 16777217, 0 },
         arithmetic::frame_header{ arithmetic::frame_kind::static_range, 1024, 4, 0 } }) {
    auto archive = valid;
    auto const wire_header = codec::encode_frame_header(header);
    std::ranges::copy(wire_header, archive.begin() + 8);
    REQUIRE_THROWS_MATCHES(decompress(archive), exception, error_is(errc::invalid_frame));
  }
  bytes source(1024, std::byte{ 'A' });
  source.insert(source.end(), 1024, std::byte{ 'B' });
  auto archive = compress(source);
  REQUIRE(archive.at(22) == std::byte{ 2 });
  archive.at(27) = std::byte{ 'A' };// Duplicate the first symbol in the second model entry.
  REQUIRE_THROWS_MATCHES(decompress(archive), exception, error_is(errc::invalid_frame));

  auto const two_frames = compress(bytes(2048, std::byte{ 'A' }), { .block_size_ = 1024 });
  recording_sink sink;
  REQUIRE_THROWS_MATCHES(
    codec::decompress_stream<arithmetic::stream_format>(two_frames, sink, { .max_output_bytes_ = 2047 }),
    exception,
    error_is(errc::output_limit_exceeded));
  REQUIRE(sink.bytes_ == bytes(1024, std::byte{ 'A' }));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST_CASE("DAC1 model precision boundaries and malformed totals have wire errors", "[arithmetic][archive]")
{
  bytes const source(128, std::byte{ 'A' });
  for (auto precision : { 1U, 16U }) {
    auto frame = arithmetic::encode_frame({ 0, source });
    frame.block_.model_ = arithmetic::make_static_model(source, precision);
    frame.block_.payload_ = arithmetic::encode_block(source, *frame.block_.model_);
    frame.header_.payload_size_ = static_cast<std::uint32_t>(frame.block_.payload_.size());
    recording_sink sink;
    arithmetic::write_archive_header(sink);
    arithmetic::write_frame(sink, frame);
    arithmetic::write_frame_header(sink, {});
    REQUIRE(decompress(sink.bytes_) == source);
    REQUIRE(sink.bytes_.at(21) == static_cast<std::byte>(precision));
  }
  auto const valid = compress(source);
  for (auto precision : { 0, 17 }) {
    auto corrupt = valid;
    corrupt.at(21) = static_cast<std::byte>(precision);
    REQUIRE_THROWS_MATCHES(decompress(corrupt), exception, error_is(errc::invalid_frame));
  }
  for (auto frequency_minus_one : { 4094U, 4096U }) {
    auto corrupt = valid;
    auto const frequency = dacoexec::detail::support::write_u16_le(static_cast<std::uint16_t>(frequency_minus_one));
    std::ranges::copy(frequency, corrupt.begin() + 25);
    REQUIRE_THROWS_MATCHES(decompress(corrupt), exception, error_is(errc::invalid_frame));
  }
}

TEST_CASE("DAC1 range payload termination respects the declared boundary", "[arithmetic][archive]")
{
  bytes source;
  for (unsigned i = 0; i < 1024; ++i) { source.push_back(static_cast<std::byte>(i % 2)); }
  auto const valid = compress(source);
  auto const frame_header = arithmetic::read_frame_header(
    std::span<std::byte const, arithmetic::frame_header_size>{ valid.data() + 8, arithmetic::frame_header_size });
  REQUIRE(frame_header.payload_size_ > arithmetic::range_flush_bytes);
  auto const payload_end = valid.size() - arithmetic::frame_header_size;
  for (auto append : { false, true }) {
    auto corrupt = valid;
    auto header = frame_header;
    if (append) {
      corrupt.insert(corrupt.begin() + static_cast<std::ptrdiff_t>(payload_end), std::byte{ 0 });
      ++header.payload_size_;
    } else {
      corrupt.erase(corrupt.begin() + static_cast<std::ptrdiff_t>(payload_end - 1));
      --header.payload_size_;
    }
    auto const wire_header = codec::encode_frame_header(header);
    std::ranges::copy(wire_header, corrupt.begin() + 8);
    REQUIRE_THROWS_MATCHES(decompress(corrupt), exception, error_is(errc::invalid_payload));
  }
}

TEST_CASE("DAC1 accepts the maximum frame size and rejects one byte above", "[arithmetic][archive]")
{
  auto const bound = arithmetic::max_frame_size;
  bytes source(bound, std::byte{ 'A' });
  auto const archive = compress(source, { .block_size_ = bound });
  REQUIRE(decompress(archive, { .max_frame_size_ = bound, .max_output_bytes_ = bound }) == source);
  source.push_back(std::byte{ 'A' });
  REQUIRE_THROWS_MATCHES(arithmetic::encode_frame({ 0, std::move(source) }), exception, error_is(errc::invalid_frame));
  for (auto kind : { arithmetic::frame_kind::raw, arithmetic::frame_kind::static_range }) {
    auto header =
      arithmetic::frame_header{ kind, static_cast<std::uint32_t>(bound), static_cast<std::uint32_t>(bound), 0 };
    REQUIRE_NOTHROW(arithmetic::validate_frame_header(header));
    ++header.payload_size_;
    REQUIRE_THROWS_MATCHES(arithmetic::validate_frame_header(header), exception, error_is(errc::invalid_frame));
    --header.payload_size_;
    ++header.uncompressed_size_;
    REQUIRE_THROWS_MATCHES(arithmetic::validate_frame_header(header), exception, error_is(errc::invalid_frame));
  }
}

TEST_CASE("DAC1 compression consumes single pass input", "[arithmetic][archive]")
{
  std::string const source = std::string(1024, 'A') + std::string(1024, 'B') + "tail";
  std::istringstream input(source);
  auto range = std::ranges::subrange(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  recording_sink sink;
  auto const stats = codec::compress_stream<arithmetic::stream_format>(range, sink, { .block_size_ = 512 });
  bytes expected;
  for (auto byte : source) { expected.push_back(static_cast<std::byte>(byte)); }
  REQUIRE(stats.input_bytes_ == source.size());
  REQUIRE(stats.frames_ == 5);
  REQUIRE(sink.bytes_ == compress(expected, { .block_size_ = 512 }));
  REQUIRE(decompress(sink.bytes_) == expected);
}

TEST_CASE("DAC1 bounded randomized mutations preserve error categories", "[arithmetic][archive]")
{
  bytes source;
  std::mt19937 generator{ 12345 };
  for (unsigned index = 0; index < 2048; ++index) { source.push_back(static_cast<std::byte>(generator() % 4)); }
  auto const valid = compress(source);
  for (unsigned trial = 0; trial < 256; ++trial) {
    auto corrupt = valid;
    auto const position = static_cast<std::size_t>(generator()) % corrupt.size();
    corrupt.at(position) ^= static_cast<std::byte>(1U << (generator() % 8));
    recording_sink sink;
    try {
      auto const stats = codec::decompress_stream<arithmetic::stream_format>(
        corrupt, sink, { .max_frame_size_ = 4096, .max_output_bytes_ = 4096 });
      REQUIRE(stats.output_bytes_ == source.size());
      REQUIRE(sink.bytes_ == source);
    } catch (exception const &error) {
      auto const code = error.code();
      REQUIRE((code == errc::invalid_archive || code == errc::invalid_frame || code == errc::invalid_payload
               || code == errc::truncated_archive || code == errc::checksum_mismatch
               || code == errc::output_limit_exceeded || code == errc::unsupported_coder));
      // END mutations can fail after the verified data frame was published.
      REQUIRE((sink.bytes_.empty() || sink.bytes_ == source));
    }
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST_CASE("Shared owning frame views preserve cursors and terminal failure", "[arithmetic][ans][archive][ranges]")
{
  auto check = []<class Format>() {
    recording_sink sink;
    auto const stats = codec::compress_stream<Format>(bytes(2048, std::byte{ 'A' }), sink, { .block_size_ = 1024 });
    REQUIRE(stats.frames_ == 2);
    // Construction reads nothing, even when the archive header is malformed.
    auto invalid = sink.bytes_;
    invalid.front() = std::byte{ 0 };
    auto lazy = Format::frames(invalid, 1024);
    REQUIRE_THROWS_MATCHES(lazy.begin(), exception, error_is(errc::invalid_archive));
    REQUIRE(lazy.begin() == lazy.end());

    sink.bytes_.resize(sink.bytes_.size() - Format::frame_header_size);
    auto original = Format::frames(sink.bytes_, 1024);
    auto cursor = original.begin();
    auto copied = cursor;
    auto moved = std::move(original);
    REQUIRE(original.begin() == original.end());
    REQUIRE((*cursor).block_.sequence_ == 0);
    ++cursor;
    REQUIRE((*copied).block_.sequence_ == 1);
    REQUIRE((*moved.begin()).block_.sequence_ == 1);
    REQUIRE_THROWS_MATCHES(++cursor, exception, error_is(errc::truncated_archive));
    REQUIRE(copied == moved.end());
    REQUIRE_NOTHROW(++cursor);
    REQUIRE(cursor == moved.end());
  };
  check.template operator()<arithmetic::stream_format>();
  check.template operator()<dacoexec::detail::ans::stream_format>();
}
