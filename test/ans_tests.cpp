#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/detail/ans/block.hpp>
#include <dacoexec/detail/ans/encoded_block.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/ans/model_wire.hpp>
#include <dacoexec/detail/ans/rans/codec.hpp>
#include <dacoexec/detail/ans/stream.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/model/frequency_model.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/detail/views/ans.hpp>
#include <dacoexec/detail/views/ans_frames.hpp>
#include <dacoexec/detail/views/blocks.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <limits>
#include <list>
#include <random>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace ans = dacoexec::detail::ans;
using bytes = std::vector<std::byte>;
constexpr std::size_t block_size = 4096;
constexpr unsigned seed = 1729;
constexpr std::size_t sequence = 42;

auto archive(bytes const &input) -> bytes
{
  bytes output;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(output) };
  auto const stats = ans::compress_stream(input, sink, { block_size });
  REQUIRE(stats.input_bytes_ == input.size());
  REQUIRE(stats.output_bytes_ == output.size());
  return output;
}

auto restore(bytes const &input, std::uint64_t limit = std::numeric_limits<std::uint64_t>::max()) -> bytes
{
  bytes output;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(output) };
  auto const stats = ans::decompress_stream(input, sink, { .max_frame_size_ = block_size, .max_output_bytes_ = limit });
  REQUIRE(stats.input_bytes_ == input.size());
  REQUIRE(stats.output_bytes_ == output.size());
  return output;
}
}// namespace

TEST_CASE("rANS has a stable wire vector and rejects symbols absent from its model", "[ans]")
{
  dacoexec::detail::model::frequency_table counts{};
  counts.at(0) = 1;
  counts.at(1) = 1;
  auto const model = dacoexec::detail::model::make_model(counts, 1);
  bytes input{ std::byte{ 0 }, std::byte{ 1 }, std::byte{ 0 }, std::byte{ 1 } };
  bytes const expected{ std::byte{ 0x0a }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0x08 } };
  auto const encoded = ans::rans::encode_block(input, model);
  REQUIRE(encoded == expected);
  REQUIRE(ans::rans::decode_block(encoded, model, input.size()) == input);
  bytes const invalid{ std::byte{ 0 }, std::byte{ 2 } };
  REQUIRE_THROWS_MATCHES(ans::rans::encode_block(invalid, model),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  counts.at(1) = 0;
  auto const single = dacoexec::detail::model::make_model(counts);
  REQUIRE_THROWS_MATCHES(ans::rans::encode_block(input, single),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  bytes repeated(block_size, std::byte{ 0 });
  auto const compact = ans::rans::encode_block(repeated, single);
  REQUIRE(compact.size() == sizeof(std::uint32_t));
  REQUIRE(ans::rans::decode_block(compact, single, repeated.size()) == repeated);
}

TEST_CASE("rANS round trips byte alphabets at every supported precision", "[ans]")
{
  std::mt19937 random{ seed };
  bytes input(block_size);
  std::ranges::generate(input, [&random]() -> std::byte { return static_cast<std::byte>(random()); });
  for (unsigned precision = dacoexec::detail::support::bits_per_byte;
    precision <= dacoexec::detail::model::max_precision_bits;
    ++precision) {
    auto const model =
      dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(input), precision);
    auto const encoded = ans::rans::encode_block(input, model);
    REQUIRE(ans::rans::decode_block(encoded, model, input.size()) == input);
    REQUIRE_THROWS_MATCHES(ans::rans::decode_block(encoded, model, input.size(), input.size() - 1),
      dacoexec::exception,
      dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
    for (std::size_t size = 0; size < encoded.size(); ++size) {
      REQUIRE_THROWS_MATCHES(ans::rans::decode_block(std::span(encoded).first(size), model, input.size()),
        dacoexec::exception,
        dacoexec::test::error_is(dacoexec::errc::invalid_payload));
    }
    auto trailing = encoded;
    trailing.push_back(std::byte{ 0 });
    REQUIRE_THROWS_MATCHES(ans::rans::decode_block(trailing, model, input.size()),
      dacoexec::exception,
      dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  }
}

TEST_CASE("ANS blocks choose RAW based on complete model cost", "[ans]")
{
  bytes repeated(block_size, std::byte{ 'A' });
  auto encoded = ans::compress_block({ sequence, repeated });
  REQUIRE(encoded.encoding_ == ans::block_encoding::rans);
  auto decoded = ans::decompress_block(std::move(encoded), repeated.size());
  REQUIRE(decoded.sequence_ == sequence);
  REQUIRE(decoded.bytes_ == repeated);
  bytes tiny{ std::byte{ 'A' } };
  auto raw = ans::compress_block({ sequence, tiny });
  REQUIRE(raw.encoding_ == ans::block_encoding::raw);
  auto const decoded_raw = ans::decompress_block(std::move(raw), tiny.size());
  REQUIRE(decoded_raw.bytes_ == tiny);
  auto empty = ans::compress_block({ sequence, {} });
  auto const decoded_empty = ans::decompress_block(std::move(empty), 0);
  REQUIRE(decoded_empty.bytes_.empty());
}

TEST_CASE("DAN1 validates framing, checksums, models and output limits", "[ans]")
{
  bytes input(block_size + 1, std::byte{ 'A' });
  auto const encoded = archive(input);
  REQUIRE(restore(encoded) == input);
  REQUIRE(restore(archive({})).empty());
  REQUIRE_THROWS_MATCHES(restore(encoded, input.size() - 1),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  auto trailing = encoded;
  trailing.push_back(std::byte{ 0 });
  REQUIRE_THROWS_MATCHES(
    restore(trailing), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_archive));
  for (std::size_t size = 0; size < encoded.size(); ++size) {
    REQUIRE_THROWS_MATCHES(restore(bytes(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(size))),
      dacoexec::exception,
      dacoexec::test::error_is(dacoexec::errc::truncated_archive));
  }
  auto corrupt = encoded;
  corrupt.at(ans::archive_header_size + ans::crc_field_offset) ^= std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(
    restore(corrupt), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::checksum_mismatch));
  corrupt = encoded;
  corrupt.at(ans::archive_header_size + ans::frame_header_size) = std::byte{ 0 };
  REQUIRE_THROWS_MATCHES(
    restore(corrupt), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_frame));
  corrupt = encoded;
  auto const first_frequency =
    ans::archive_header_size + ans::frame_header_size + ans::model_prefix_size + sizeof(std::uint8_t);
  corrupt.at(first_frequency) = std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(
    restore(corrupt), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_frame));
}

TEST_CASE("ANS adaptors consume independent owned blocks", "[ans]")
{
  bytes input(block_size + 1, std::byte{ 'A' });
  auto model = dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(input));
  auto blocks = input | dacoexec::detail::views::blocks(block_size) | dacoexec::detail::views::encode::rans(model)
                | dacoexec::detail::views::decode::rans(block_size);
  bytes decoded;
  std::uint64_t next = 0;
  for (auto const &block : blocks) {
    REQUIRE(block.sequence_ == next++);
    decoded.insert(decoded.end(), block.bytes_.begin(), block.bytes_.end());
  }
  REQUIRE(decoded == input);
}

TEST_CASE("rANS rejects invalid decoder states", "[ans]")
{
  dacoexec::detail::model::frequency_table counts{};
  counts.at(0) = 1;
  counts.at(1) = 1;
  auto const model = dacoexec::detail::model::make_model(counts);
  bytes const empty;
  auto const encoded_empty = ans::rans::encode_block(empty, model);
  REQUIRE(ans::rans::decode_block(encoded_empty, model, 0).empty());
  bytes invalid_state(sizeof(std::uint32_t), std::byte{ 0 });
  REQUIRE_THROWS_MATCHES(ans::rans::decode_block(invalid_state, model, 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  constexpr auto all_bits_set = std::numeric_limits<unsigned char>::max();
  invalid_state.assign(sizeof(std::uint32_t), static_cast<std::byte>(all_bits_set));
  REQUIRE_THROWS_MATCHES(ans::rans::decode_block(invalid_state, model, 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  auto wrong_terminal = encoded_empty;
  wrong_terminal.front() ^= std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(ans::rans::decode_block(wrong_terminal, model, 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
}

TEST_CASE("ANS block views stage once and preserve borrowed dereferences", "[ans][ranges]")
{
  bytes input(block_size + 1, std::byte{ 'A' });
  auto const model = dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(input));
  std::size_t reads = 0;
  auto source = input | std::views::transform([&reads](std::byte value) -> std::byte {
    ++reads;
    return value;
  });
  auto view = source | dacoexec::detail::views::blocks(block_size) | dacoexec::detail::views::encode::rans(model);
  REQUIRE(reads == 0);
  auto cursor = view.begin();
  auto const staged_reads = reads;
  auto const *staged = &*cursor;
  auto const first = *cursor;
  auto const second = *cursor;
  REQUIRE(first.payload_ == second.payload_);
  REQUIRE(first.uncompressed_size_ == block_size);
  REQUIRE(&*view.begin() == staged);
  REQUIRE(reads == staged_reads);
  auto moved = std::move(view);
  REQUIRE(&*cursor == staged);
  REQUIRE(&*moved.begin() == staged);
  auto transferred = std::ranges::iter_move(cursor);
  REQUIRE(transferred.payload_ == first.payload_);
  ++cursor;
  REQUIRE((*cursor).sequence_ == 1);
}

TEST_CASE("ANS decoded views preserve the staged decoded block", "[ans][ranges]")
{
  bytes input(block_size, std::byte{ 'A' });
  auto const model = dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(input));
  auto view = input | dacoexec::detail::views::blocks(block_size) | dacoexec::detail::views::encode::rans(model)
              | dacoexec::detail::views::decode::rans(block_size);
  auto cursor = view.begin();
  auto const *first = &*cursor;
  REQUIRE(first->bytes_ == input);
  REQUIRE(&*cursor == first);
  REQUIRE((*cursor).bytes_ == input);
  auto owned = std::ranges::iter_move(cursor);
  REQUIRE(owned.bytes_ == input);
  ++cursor;
  REQUIRE(cursor == view.end());
}

TEST_CASE("DAN1 sparse model cost includes only present symbols", "[ans]")
{
  constexpr std::size_t single_symbol_model_size = 6;
  constexpr std::size_t dense_model_size = 771;
  auto const single =
    dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(bytes{ std::byte{ 'A' } }));
  REQUIRE(ans::model_wire_size(single) == single_symbol_model_size);
  dacoexec::detail::model::frequency_table counts{};
  counts.fill(1);
  REQUIRE(ans::model_wire_size(dacoexec::detail::model::make_model(counts)) == dense_model_size);
  bytes input(block_size, std::byte{ 'A' });
  auto const encoded = archive(input);
  REQUIRE(
    encoded.size()
    == ans::archive_header_size + (2 * ans::frame_header_size) + single_symbol_model_size + sizeof(std::uint32_t));
  auto corrupt = encoded;
  auto const model_offset = ans::archive_header_size + ans::frame_header_size;
  auto const entry_count_offset = model_offset + sizeof(std::uint8_t);
  corrupt.at(entry_count_offset) = std::byte{ 0 };
  REQUIRE_THROWS_MATCHES(
    restore(corrupt), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_frame));
  auto const maximum_model = dacoexec::detail::model::make_model(
    dacoexec::detail::model::count_frequencies(input), dacoexec::detail::model::max_precision_bits);
  ans::encoded_frame frame;
  frame.block_ = { .encoding_ = ans::block_encoding::rans,
    .uncompressed_size_ = input.size(),
    .payload_ = ans::rans::encode_block(input, maximum_model),
    .model_ = maximum_model };
  frame.header_ = { .kind_ = ans::frame_kind::rans,
    .uncompressed_size_ = static_cast<std::uint32_t>(input.size()),
    .payload_size_ = static_cast<std::uint32_t>(frame.block_.payload_.size()),
    .crc32_ = dacoexec::detail::support::crc32(input) };
  bytes maximum_archive;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(maximum_archive) };
  ans::write_archive_header(sink);
  ans::write_frame(sink, frame);
  ans::write_frame_header(sink, {});
  REQUIRE(restore(maximum_archive) == input);
}

TEST_CASE("DAN1 sparse models require unique sorted symbols", "[ans]")
{
  bytes input(block_size, std::byte{ 'A' });
  input.back() = std::byte{ 'B' };
  auto const encoded = archive(input);
  REQUIRE(restore(encoded) == input);
  auto const model_offset = ans::archive_header_size + ans::frame_header_size;
  auto const first_symbol_offset = model_offset + ans::model_prefix_size;
  auto const second_symbol_offset = first_symbol_offset + ans::model_entry_size;
  auto duplicate = encoded;
  duplicate.at(second_symbol_offset) = duplicate.at(first_symbol_offset);
  REQUIRE_THROWS_MATCHES(
    restore(duplicate), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_frame));
  auto unordered = encoded;
  std::swap(unordered.at(first_symbol_offset), unordered.at(second_symbol_offset));
  REQUIRE_THROWS_MATCHES(
    restore(unordered), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_frame));
  auto oversized = encoded;
  auto const count_offset = model_offset + sizeof(std::uint8_t);
  auto const invalid_count =
    dacoexec::detail::support::write_u16_le(static_cast<std::uint16_t>(dacoexec::detail::model::alphabet_size + 1));
  std::ranges::copy(invalid_count, oversized.begin() + static_cast<std::ptrdiff_t>(count_offset));
  REQUIRE_THROWS_MATCHES(
    restore(oversized), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_frame));
}

TEST_CASE("ANS staged views leave failed passes ended", "[ans][ranges]")
{
  bytes input(block_size, std::byte{ 'B' });
  auto const model =
    dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(bytes{ std::byte{ 'A' } }));
  auto view = input | dacoexec::detail::views::blocks(block_size) | dacoexec::detail::views::encode::rans(model);
  REQUIRE_THROWS_MATCHES(view.begin(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE(view.begin() == view.end());
}

namespace {
auto wire_bytes(std::initializer_list<unsigned char> values) -> bytes
{
  bytes result;
  std::ranges::transform(
    values, std::back_inserter(result), [](unsigned char value) -> std::byte { return static_cast<std::byte>(value); });
  return result;
}
}// namespace

TEST_CASE("DAN1 serialization matches golden wire vectors", "[ans][wire]")
{
  auto const empty_wire = wire_bytes({ 0x44,
    0x41,
    0x4e,
    0x31,
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
    0x00 });
  auto const raw_wire = wire_bytes({ 0x44,
    0x41,
    0x4e,
    0x31,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x8b,
    0x9e,
    0xd9,
    0xd3,
    0x41,
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
    0x00 });
  auto const repeated_wire = wire_bytes({ 0x44,
    0x41,
    0x4e,
    0x31,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x00,
    0x10,
    0x00,
    0x00,
    0x04,
    0x00,
    0x00,
    0x00,
    0x40,
    0x34,
    0xa6,
    0xfe,
    0x0c,
    0x01,
    0x00,
    0x41,
    0xff,
    0x0f,
    0x00,
    0x00,
    0x80,
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
    0x00 });
  auto const two_symbol_wire = wire_bytes({ 0x44,
    0x41,
    0x4e,
    0x31,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x03,
    0x00,
    0x00,
    0x00,
    0x04,
    0x00,
    0x00,
    0x00,
    0x64,
    0x62,
    0x8d,
    0x4d,
    0x01,
    0x02,
    0x00,
    0x41,
    0x00,
    0x00,
    0x42,
    0x00,
    0x00,
    0x02,
    0x00,
    0x00,
    0x04,
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
    0x00 });
  REQUIRE(archive({}) == empty_wire);
  REQUIRE(archive(bytes{ std::byte{ 'A' } }) == raw_wire);
  REQUIRE(archive(bytes(block_size, std::byte{ 'A' })) == repeated_wire);
  REQUIRE(restore(empty_wire).empty());
  REQUIRE(restore(raw_wire) == bytes{ std::byte{ 'A' } });
  REQUIRE(restore(repeated_wire) == bytes(block_size, std::byte{ 'A' }));

  bytes const input{ std::byte{ 'A' }, std::byte{ 'B' }, std::byte{ 'A' } };
  dacoexec::detail::model::frequency_table counts{};
  counts.at('A') = 1;
  counts.at('B') = 1;
  auto const model = dacoexec::detail::model::make_model(counts, 1);
  ans::encoded_frame frame;
  frame.block_ = { .encoding_ = ans::block_encoding::rans,
    .uncompressed_size_ = input.size(),
    .payload_ = ans::rans::encode_block(input, model),
    .model_ = model };
  frame.header_ = { .kind_ = ans::frame_kind::rans,
    .uncompressed_size_ = static_cast<std::uint32_t>(input.size()),
    .payload_size_ = static_cast<std::uint32_t>(frame.block_.payload_.size()),
    .crc32_ = dacoexec::detail::support::crc32(input) };
  bytes encoded;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(encoded) };
  ans::write_archive_header(sink);
  ans::write_frame(sink, frame);
  ans::write_frame_header(sink, {});
  REQUIRE(encoded == two_symbol_wire);
  REQUIRE(restore(two_symbol_wire) == input);
}

TEST_CASE("ANS frame dereference borrows until explicit ownership transfer", "[ans][ranges]")
{
  auto const encoded = archive(bytes(block_size + 1, std::byte{ 'A' }));
  auto frames = encoded | dacoexec::detail::views::ans_frames();
  auto cursor = frames.begin();
  auto const &first = *cursor;
  auto const *staged = &first;
  auto const payload = first.block_.payload_;
  REQUIRE(payload.size() == sizeof(std::uint32_t));
  REQUIRE(&*cursor == staged);
  REQUIRE((*cursor).block_.payload_ == payload);
  REQUIRE(&*frames.begin() == staged);
  auto moved = std::move(frames);
  REQUIRE(&*moved.begin() == staged);
  auto owned = std::ranges::iter_move(cursor);
  REQUIRE(owned.block_.payload_ == payload);
  ++cursor;
  REQUIRE((*cursor).block_.sequence_ == 1);
  REQUIRE((*cursor).block_.payload_ == bytes{ std::byte{ 'A' } });
  REQUIRE(owned.block_.payload_ == payload);
  ++cursor;
  REQUIRE(cursor == moved.end());
}

TEST_CASE("rANS produces identical payloads across range categories", "[ans][ranges]")
{
  bytes input(block_size);
  for (std::size_t index = 0; index < input.size(); ++index) {
    input.at(index) = static_cast<std::byte>(index % dacoexec::detail::model::alphabet_size);
  }
  auto const model = dacoexec::detail::model::make_model(dacoexec::detail::model::count_frequencies(input));
  auto const expected = ans::rans::encode_block(input, model);
  std::list<std::byte> linked(input.begin(), input.end());
  REQUIRE(ans::rans::encode_block(linked, model) == expected);
  auto noncommon = std::ranges::subrange(
    std::counted_iterator(linked.begin(), static_cast<std::ptrdiff_t>(linked.size())), std::default_sentinel);
  REQUIRE(ans::rans::encode_block(noncommon, model) == expected);
  std::forward_list<std::byte> const forward(input.begin(), input.end());
  REQUIRE(ans::rans::encode_block(forward, model) == expected);
  std::string text;
  std::ranges::transform(input, std::back_inserter(text), [](std::byte byte) -> char {
    return static_cast<char>(std::to_integer<unsigned char>(byte));
  });
  std::istringstream stream{ text };
  auto single_pass = std::ranges::istream_view<char>(stream >> std::noskipws);
  REQUIRE(ans::rans::encode_block(single_pass, model) == expected);
  REQUIRE(ans::rans::decode_block(expected, model, input.size()) == input);
}

TEST_CASE("DAN1 writes sparse metadata in one sink call", "[ans][wire]")
{
  struct recording_sink
  {
    std::vector<std::size_t> sizes_;

    auto write(std::span<std::byte const> data) -> void { sizes_.push_back(data.size()); }
  } sink;

  auto frame = ans::encode_frame({ 0, bytes(block_size, std::byte{ 'A' }) });
  ans::write_frame(sink, frame);
  REQUIRE(frame.block_.model_.has_value());
  if (frame.block_.model_) {
    REQUIRE(sink.sizes_
            == std::vector<std::size_t>{
              ans::frame_header_size, ans::model_wire_size(*frame.block_.model_), frame.block_.payload_.size() });
  }
}
