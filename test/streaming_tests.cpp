#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/detail/crc32.hpp>
#include <dacoexec/detail/endian.hpp>
#include <dacoexec/detail/error.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/canonical.hpp>
#include <dacoexec/huffman/format.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/stream.hpp>
#include <dacoexec/huffman/tree.hpp>
#include <dacoexec/io/output_sink.hpp>
#include <dacoexec/views/frames.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using bytes = std::vector<std::byte>;
namespace huffman = dacoexec::huffman;
constexpr std::size_t test_block_size = 1024;

auto compress(bytes const &input, std::size_t block_size = test_block_size) -> bytes
{
  bytes archive;
  dacoexec::io::iterator_sink sink(std::back_inserter(archive));
  auto const statistics = huffman::compress_stream(input, sink, { block_size });
  REQUIRE(statistics.input_bytes_ == input.size());
  REQUIRE(statistics.output_bytes_ == archive.size());
  return archive;
}

auto decompress(bytes const &archive, std::uint64_t limit = std::numeric_limits<std::uint64_t>::max()) -> bytes
{
  bytes output;
  dacoexec::io::iterator_sink sink(std::back_inserter(output));
  auto const statistics = huffman::decompress_stream(
    archive, sink, { .max_frame_size_ = huffman::max_frame_size, .max_output_bytes_ = limit });
  REQUIRE(statistics.input_bytes_ == archive.size());
  REQUIRE(statistics.output_bytes_ == output.size());
  return output;
}

auto manual_archive(huffman::frame_header header,
  bytes const &payload = {},
  huffman::code_length_table const &lengths = {}) -> bytes
{
  bytes archive;
  dacoexec::io::iterator_sink sink(std::back_inserter(archive));
  huffman::write_archive_header(sink);
  huffman::write_frame_header(sink, header);
  if (header.kind_ == huffman::frame_kind::huffman) {
    for (auto length : lengths) { archive.push_back(static_cast<std::byte>(length)); }
  }
  sink.write(payload);
  huffman::write_frame_header(sink, {});
  return archive;
}
}// namespace

TEST_CASE("DHF1 fixed bytes and IEEE CRC32", "[streaming]")
{
  auto const empty = compress({});
  bytes const golden{ std::byte{ 'D' },
    std::byte{ 'H' },
    std::byte{ 'F' },
    std::byte{ '1' },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0xFF },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 } };
  REQUIRE(empty == golden);
  REQUIRE(decompress(golden).empty());
  constexpr std::uint32_t integer = 0x78563412;
  constexpr std::array expected{ std::byte{ 0x12 }, std::byte{ 0x34 }, std::byte{ 0x56 }, std::byte{ 0x78 } };
  REQUIRE(dacoexec::detail::write_u32_le(integer) == expected);
  REQUIRE(dacoexec::detail::read_u32_le(expected) == integer);
  constexpr std::uint16_t short_integer = 0x3412;
  constexpr std::array short_expected{ std::byte{ 0x12 }, std::byte{ 0x34 } };
  REQUIRE(dacoexec::detail::write_u16_le(short_integer) == short_expected);
  REQUIRE(dacoexec::detail::read_u16_le(short_expected) == short_integer);
  bytes digits;
  for (char character : std::string_view("123456789")) { digits.push_back(static_cast<std::byte>(character)); }
  REQUIRE(dacoexec::detail::crc32(digits) == 0xCBF43926U);
}

TEST_CASE("DHF1 deterministic mixed block round trips", "[streaming]")
{
  for (auto size : { std::size_t{ 0 }, std::size_t{ 1 }, test_block_size, test_block_size + 1, test_block_size * 3 }) {
    bytes input(size, std::byte{ 0 });
    for (std::size_t index = test_block_size; index < std::min(size, test_block_size * 2); ++index) {
      input.at(index) = static_cast<std::byte>(index);
    }
    auto const archive = compress(input);
    REQUIRE(compress(input) == archive);
    REQUIRE(decompress(archive) == input);
    std::size_t frames = 0;
    for (auto frame : archive | dacoexec::views::frames()) {
      auto const expected_kind = frames == 1 || size == 1 ? huffman::frame_kind::raw : huffman::frame_kind::huffman;
      if (frame.header_.uncompressed_size_ == test_block_size) { REQUIRE(frame.header_.kind_ == expected_kind); }
      ++frames;
    }
    REQUIRE(frames == (size / test_block_size) + (size % test_block_size != 0 ? 1U : 0U));
  }
}

TEST_CASE("DHF1 independently rejects malformed structure", "[streaming]")
{
  auto const valid = compress(bytes{ std::byte{ 'x' } });
  for (std::size_t length = 0; length < valid.size(); ++length) {
    auto truncated = valid;
    truncated.resize(length);
    REQUIRE_THROWS(decompress(truncated));
  }
  for (auto offset : { std::size_t{ 0 },
         std::size_t{ 4 },
         std::size_t{ 6 },
         huffman::archive_header_size,
         huffman::archive_header_size + huffman::crc_field_offset }) {
    auto corrupt = valid;
    constexpr auto corruption_mask = std::byte{ 0x40 };
    corrupt.at(offset) ^= corruption_mask;
    REQUIRE_THROWS(decompress(corrupt));
  }
  auto trailing = valid;
  trailing.push_back(std::byte{ 0 });
  REQUIRE_THROWS(decompress(trailing));
  auto end = compress({});
  end.at(huffman::archive_header_size + huffman::size_field_offset) = std::byte{ 1 };
  REQUIRE_THROWS(decompress(end));
  auto mismatch = valid;
  mismatch.at(huffman::archive_header_size + huffman::payload_field_offset) = std::byte{ 2 };
  REQUIRE_THROWS(decompress(mismatch));
  auto oversized = valid;
  auto const enormous = dacoexec::detail::write_u32_le(std::numeric_limits<std::uint32_t>::max());
  std::ranges::copy(enormous,
    oversized.begin() + static_cast<std::ptrdiff_t>(huffman::archive_header_size + huffman::payload_field_offset));
  REQUIRE_THROWS(decompress(oversized));
  REQUIRE_THROWS(decompress(valid, 0));
  auto zero = valid;
  zero.at(huffman::archive_header_size + huffman::size_field_offset) = std::byte{ 0 };
  REQUIRE_THROWS(decompress(zero));
}

TEST_CASE("DHF1 Huffman validation and inferred padding", "[streaming]")
{
  bytes const input(test_block_size + 1, std::byte{ 'a' });
  auto const valid = compress(input, input.size());
  REQUIRE(decompress(valid) == input);
  constexpr auto payload_offset = huffman::archive_header_size + huffman::frame_header_size + huffman::alphabet_size;
  auto padding = valid;
  auto const payload_size = (input.size() + dacoexec::detail::bits_per_byte - 1U) / dacoexec::detail::bits_per_byte;
  padding.at(payload_offset + payload_size - 1) |= std::byte{ 1 };
  REQUIRE_THROWS(decompress(padding));
  auto extra = valid;
  extra.insert(extra.begin() + static_cast<std::ptrdiff_t>(payload_offset + payload_size), std::byte{ 0 });
  auto const enlarged = dacoexec::detail::write_u32_le(static_cast<std::uint32_t>(payload_size + 1));
  std::ranges::copy(enlarged,
    extra.begin() + static_cast<std::ptrdiff_t>(huffman::archive_header_size + huffman::payload_field_offset));
  REQUIRE_THROWS(decompress(extra));
  huffman::code_length_table lengths{};
  lengths.at(0) = 1;
  lengths.at(1) = 1;
  lengths.at(2) = 1;
  REQUIRE_THROWS(decompress(
    manual_archive({ .kind_ = huffman::frame_kind::huffman, .uncompressed_size_ = 1, .payload_size_ = 1, .crc32_ = 0 },
      { std::byte{ 0 } },
      lengths)));
  lengths.fill(0);
  lengths.at(0) = 2;
  lengths.at(1) = 2;
  REQUIRE_THROWS(decompress(
    manual_archive({ .kind_ = huffman::frame_kind::huffman, .uncompressed_size_ = 1, .payload_size_ = 1, .crc32_ = 0 },
      { std::byte{ 0 } },
      lengths)));
  // Complete tree: 0, 10, 11. Nine declared symbols cannot fit one byte.
  lengths.at(0) = 1;
  lengths.at(2) = 2;
  REQUIRE_THROWS(decompress(
    manual_archive({ .kind_ = huffman::frame_kind::huffman, .uncompressed_size_ = 8, .payload_size_ = 1, .crc32_ = 0 },
      { std::byte{ 1 } },
      lengths)));
  REQUIRE_THROWS(decompress(
    manual_archive({ .kind_ = huffman::frame_kind::huffman, .uncompressed_size_ = 9, .payload_size_ = 1, .crc32_ = 0 },
      { std::byte{ 0 } },
      lengths)));
}

TEST_CASE("DHF1 lazy state, move ownership, and permanent failure", "[streaming]")
{
  auto archive = compress(bytes(test_block_size * 2, std::byte{ 0 }));
  std::size_t consumed = 0;
  auto source = archive | std::views::transform([&consumed](auto byte) -> std::byte {
    ++consumed;
    return byte;
  });
  auto frames = source | dacoexec::views::frames();
  REQUIRE(consumed == 0);
  auto cursor = frames.begin();
  REQUIRE(consumed < archive.size());
  auto moved = std::move(frames);
  auto first = *cursor;
  REQUIRE(first.header_.uncompressed_size_ == test_block_size);
  ++cursor;
  REQUIRE(cursor != moved.end());
  ++cursor;
  REQUIRE(cursor == moved.end());
  REQUIRE(consumed == archive.size());
  auto broken_source =
    archive | std::views::transform([](auto) -> std::byte { throw std::logic_error("source failure"); });
  auto broken = broken_source | dacoexec::views::frames();
  REQUIRE_THROWS_AS(broken.begin(), std::logic_error);
  REQUIRE(broken.begin() == broken.end());
}

TEST_CASE("DHF1 stops on sink failures and writes incrementally", "[streaming]")
{
  std::size_t consumed = 0;
  auto source =
    std::views::iota(std::size_t{ 0 }, test_block_size * 3) | std::views::transform([&consumed](auto) -> std::byte {
      ++consumed;
      return std::byte{ 0 };
    });

  struct sink_type
  {
    std::reference_wrapper<std::size_t> consumed_;
    std::size_t calls_ = 0;

    auto write([[maybe_unused]] std::span<std::byte const> bytes) -> void
    {
      ++calls_;
      if (calls_ == 2) {
        REQUIRE(consumed_.get() == test_block_size);
        throw std::logic_error("sink failure");
      }
    }
  } sink{ .consumed_ = std::ref(consumed) };

  REQUIRE_THROWS_AS(huffman::compress_stream(source, sink, { test_block_size }), std::logic_error);
  REQUIRE(consumed == test_block_size);
  std::uint64_t total = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_THROWS_AS(huffman::detail::checked_add(total, 1), std::overflow_error);
}

TEST_CASE("DHF1 independently authored binary fixtures", "[streaming]")
{
  for (auto const *name : { "dhf1-empty", "dhf1-one-byte" }) {
    auto const path = std::filesystem::path(DACOEXEC_FIXTURE_DIR) / (std::string(name) + ".dhf");
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    bytes fixture;
    for (auto cursor = std::istreambuf_iterator<char>(file); cursor != std::istreambuf_iterator<char>(); ++cursor) {
      fixture.push_back(static_cast<std::byte>(static_cast<unsigned char>(*cursor)));
    }
    auto const original = std::string_view(name) == "dhf1-empty" ? bytes{} : bytes{ std::byte{ 'x' } };
    REQUIRE(compress(original) == fixture);
    REQUIRE(decompress(fixture) == original);
  }
}

TEST_CASE("DHF1 accepts move-only nonsized single-pass sources", "[streaming]")
{
  class source_type : public std::ranges::view_interface<source_type>
  {
  public:
    explicit source_type(std::string data)
      : stream_(std::make_unique<std::istringstream>(std::move(data))), view_(*stream_)
    { *stream_ >> std::noskipws; }

    source_type(source_type &&) noexcept = default;
    auto operator=(source_type &&) noexcept -> source_type & = default;
    source_type(source_type const &) = delete;
    auto operator=(source_type const &) -> source_type & = delete;
    ~source_type() = default;

    auto begin() -> decltype(std::declval<std::ranges::istream_view<char> &>().begin()) { return view_.begin(); }

    [[nodiscard]] static auto end() -> std::default_sentinel_t { return {}; }

  private:
    std::unique_ptr<std::istringstream> stream_;
    std::ranges::istream_view<char> view_;
  };

  bytes archive;
  dacoexec::io::iterator_sink archive_sink(std::back_inserter(archive));
  std::string const original(test_block_size + 1, 'x');
  auto const encoded = huffman::compress_stream(source_type(original), archive_sink, { test_block_size });
  REQUIRE(encoded.input_bytes_ == original.size());
  std::string encoded_string;
  for (auto byte : archive) { encoded_string.push_back(static_cast<char>(std::to_integer<unsigned char>(byte))); }
  bytes output;
  dacoexec::io::iterator_sink output_sink(std::back_inserter(output));
  auto const decoded = huffman::decompress_stream(source_type(std::move(encoded_string)), output_sink);
  REQUIRE(decoded.output_bytes_ == original.size());
  REQUIRE(output == bytes(original.size(), std::byte{ 'x' }));
}

TEST_CASE("DHF1 discards a failed later frame without writing it", "[streaming]")
{
  auto archive = compress(bytes(test_block_size * 2, std::byte{ 'x' }));
  auto frames = archive | dacoexec::views::frames();
  auto cursor = frames.begin();
  auto first = *cursor;
  auto const second_offset =
    huffman::archive_header_size + huffman::frame_header_size + huffman::alphabet_size + first.header_.payload_size_;
  archive.at(second_offset + huffman::crc_field_offset) ^= std::byte{ 1 };
  bytes output;
  dacoexec::io::iterator_sink sink(std::back_inserter(output));
  REQUIRE_THROWS(huffman::decompress_stream(archive, sink));
  REQUIRE(output == bytes(test_block_size, std::byte{ 'x' }));
  std::size_t consumed = 0;
  auto throwing = archive | std::views::transform([&consumed, second_offset](auto byte) -> std::byte {
    if (++consumed > second_offset + huffman::frame_header_size) { throw std::logic_error("partial frame"); }
    return byte;
  });
  auto partial = throwing | dacoexec::views::frames();
  auto position = partial.begin();
  REQUIRE(position != partial.end());
  REQUIRE_THROWS_AS(++position, std::logic_error);
  REQUIRE(position == partial.end());
}

TEST_CASE("DHF1 ostream sink reports stream errors", "[streaming]")
{
  std::ostringstream stream;
  dacoexec::io::ostream_sink sink(stream);
  stream.setstate(std::ios::badbit);
  std::array const data{ std::byte{ 0 } };
  REQUIRE_THROWS_AS(sink.write(data), dacoexec::stream_error);
}

TEST_CASE("DHF1 enforces configured limits before publishing frames", "[streaming]")
{
  auto const archive = compress(bytes(test_block_size, std::byte{ 0 }));
  auto frames = archive | dacoexec::views::frames(test_block_size - 1);
  REQUIRE_THROWS_AS(frames.begin(), dacoexec::format_error);
  REQUIRE(frames.begin() == frames.end());
  bytes output;
  dacoexec::io::iterator_sink sink(std::back_inserter(output));
  REQUIRE_THROWS_AS(huffman::compress_stream(bytes{}, sink, { 0 }), std::invalid_argument);
  REQUIRE(output.empty());
  REQUIRE_THROWS_AS(huffman::compress_stream(bytes{}, sink, { huffman::max_frame_size + 1 }), std::invalid_argument);
  REQUIRE(output.empty());
  auto const multiple = compress(bytes(test_block_size * 2, std::byte{ 0 }));
  REQUIRE_THROWS_AS(
    huffman::decompress_stream(
      multiple, sink, { .max_frame_size_ = huffman::max_frame_size, .max_output_bytes_ = test_block_size }),
    dacoexec::format_error);
  REQUIRE(output == bytes(test_block_size, std::byte{ 0 }));
}

TEST_CASE("DHF1 explicitly rejects unsupported kinds and codebooks", "[streaming]")
{
  auto unknown = compress(bytes{ std::byte{ 'x' } });
  constexpr auto unknown_kind = std::byte{ 0x7F };
  unknown.at(huffman::archive_header_size) = unknown_kind;
  SECTION("unknown kind") { REQUIRE_THROWS_AS(decompress(unknown), dacoexec::format_error); }

  huffman::code_length_table lengths{};
  auto const empty_book =
    manual_archive({ .kind_ = huffman::frame_kind::huffman, .uncompressed_size_ = 1, .payload_size_ = 1, .crc32_ = 0 },
      { std::byte{ 0 } },
      lengths);
  SECTION("all-zero codebook") { REQUIRE_THROWS_AS(decompress(empty_book), dacoexec::format_error); }

  lengths.front() = static_cast<std::uint8_t>(huffman::max_canonical_bits + 1U);
  auto const too_long =
    manual_archive({ .kind_ = huffman::frame_kind::huffman, .uncompressed_size_ = 1, .payload_size_ = 1, .crc32_ = 0 },
      { std::byte{ 0 } },
      lengths);
  SECTION("code length above supported width") { REQUIRE_THROWS_AS(decompress(too_long), dacoexec::format_error); }

  SECTION("each malformed frame immediately after a valid frame")
  {
    for (auto const &malformed : { unknown, empty_book, too_long }) {
      auto archive = compress(bytes{ std::byte{ 'x' } });
      archive.resize(archive.size() - huffman::frame_header_size);
      archive.insert(
        archive.end(), malformed.begin() + static_cast<std::ptrdiff_t>(huffman::archive_header_size), malformed.end());
      bytes output;
      dacoexec::io::iterator_sink sink(std::back_inserter(output));
      REQUIRE_THROWS_AS(huffman::decompress_stream(archive, sink), dacoexec::format_error);
      REQUIRE(output == bytes{ std::byte{ 'x' } });
    }
  }
}

TEST_CASE("DHF1 parsed DATA sequences survive view moves and decoding", "[streaming]")
{
  constexpr std::size_t frame_count = 3;
  constexpr std::uint64_t first_sequence = 0;
  constexpr std::uint64_t second_sequence = 1;
  constexpr std::uint64_t third_sequence = 2;
  bytes input(test_block_size * frame_count, std::byte{ 'x' });
  // The middle block selects RAW so both encodings share one sequence space.
  for (std::size_t index = test_block_size; index < test_block_size * 2U; ++index) {
    input.at(index) = static_cast<std::byte>(index);
  }
  auto const archive = compress(input);
  auto frames = archive | dacoexec::views::frames();
  auto cursor = frames.begin();
  auto first = *cursor;
  REQUIRE(first.block_.sequence_ == first_sequence);
  REQUIRE(first.header_.kind_ == huffman::frame_kind::huffman);
  auto moved = std::move(frames);
  // A moved-from frames_view is explicitly specified to be empty.
  // cppcheck-suppress accessMoved
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
  REQUIRE(frames.begin() == frames.end());
  ++cursor;
  auto second = *cursor;
  REQUIRE(second.block_.sequence_ == second_sequence);
  REQUIRE(second.header_.kind_ == huffman::frame_kind::raw);
  ++cursor;
  auto third = *cursor;
  REQUIRE(third.block_.sequence_ == third_sequence);
  REQUIRE(third.header_.kind_ == huffman::frame_kind::huffman);
  ++cursor;
  REQUIRE(cursor == moved.end());
  for (auto *frame : { &first, &second, &third }) {
    auto const sequence = frame->block_.sequence_;
    auto decoded = huffman::decompress_block(std::move(frame->block_), test_block_size, true);
    REQUIRE(decoded.sequence_ == sequence);
  }
}

TEST_CASE("DHF1 propagates source failure at every archive boundary", "[streaming][hardening]")
{
  struct source_failure : std::runtime_error
  {
    source_failure() : std::runtime_error("injected source failure") {}
  };

  constexpr std::size_t input_size = test_block_size + 1;
  auto const archive = compress(bytes(input_size, std::byte{ 'a' }));
  for (std::size_t boundary = 0; boundary < archive.size(); ++boundary) {
    CAPTURE(boundary);
    std::size_t reads = 0;
    auto source =
      std::views::iota(std::size_t{ 0 }, archive.size()) | std::views::transform([&](std::size_t index) -> std::byte {
        if (index == boundary) { throw source_failure{}; }
        ++reads;
        return archive.at(index);
      });
    bytes output;
    dacoexec::io::iterator_sink sink{ std::back_inserter(output) };
    REQUIRE_THROWS_AS(huffman::decompress_stream(source, sink), source_failure);
    REQUIRE(reads == boundary);
  }
}
