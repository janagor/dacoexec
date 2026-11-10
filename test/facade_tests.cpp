#include <catch2/catch_test_macros.hpp>
#include <dacoexec/ans.hpp>
#include <dacoexec/bit.hpp>
#include <dacoexec/error.hpp>
#include <dacoexec/huffman.hpp>
#include <dacoexec/views.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t partial_block_size = 511;
constexpr std::uint64_t sample_integer = 13;
constexpr std::uint64_t long_unary_integer = 1024;
constexpr unsigned byte_bits = 8;
constexpr std::uint64_t unary_limit = 128;
}// namespace

template<class R>
concept facade_byte_range = requires(R &&source, std::vector<std::byte> &output) {
  dacoexec::huffman::compress(std::forward<R>(source));
  dacoexec::huffman::decompress(std::forward<R>(source));
  dacoexec::huffman::compress_stream(std::forward<R>(source), std::back_inserter(output));
  dacoexec::huffman::decompress_stream(std::forward<R>(source), std::back_inserter(output));
  dacoexec::ans::compress(std::forward<R>(source));
  dacoexec::ans::decompress(std::forward<R>(source));
  dacoexec::ans::compress_stream(std::forward<R>(source), std::back_inserter(output));
  dacoexec::ans::decompress_stream(std::forward<R>(source), std::back_inserter(output));
};
template<class R>
concept facade_rejects_range =
  (!requires(R &&source, std::vector<std::byte> &output) { dacoexec::huffman::compress(std::forward<R>(source)); })
  && (!requires(R &&source, std::vector<std::byte> &output) { dacoexec::huffman::decompress(std::forward<R>(source)); })
  && (!requires(R &&source, std::vector<std::byte> &output) {
       dacoexec::huffman::compress_stream(std::forward<R>(source), std::back_inserter(output));
     }) && (!requires(R &&source, std::vector<std::byte> &output) {
       dacoexec::huffman::decompress_stream(std::forward<R>(source), std::back_inserter(output));
     }) && (!requires(R &&source, std::vector<std::byte> &output) { dacoexec::ans::compress(std::forward<R>(source)); })
  && (!requires(R &&source, std::vector<std::byte> &output) { dacoexec::ans::decompress(std::forward<R>(source)); })
  && (!requires(R &&source, std::vector<std::byte> &output) {
       dacoexec::ans::compress_stream(std::forward<R>(source), std::back_inserter(output));
     }) && (!requires(R &&source, std::vector<std::byte> &output) {
       dacoexec::ans::decompress_stream(std::forward<R>(source), std::back_inserter(output));
     });
static_assert(facade_byte_range<std::vector<std::byte> &>);
static_assert(facade_byte_range<std::vector<char> &>);
static_assert(facade_byte_range<std::vector<signed char> &>);
static_assert(facade_byte_range<std::vector<unsigned char> &>);
static_assert(facade_byte_range<std::vector<char8_t> &>);
static_assert(facade_rejects_range<std::vector<int> &>);
static_assert(facade_rejects_range<std::vector<bool> &>);
static_assert(facade_rejects_range<std::vector<char16_t> &>);
static_assert(facade_rejects_range<int>);

TEST_CASE("Public codecs round trip complete archives with bounded streaming output", "[facade]")
{
  std::string const text(4096, 'a');
  auto input = text | std::views::transform([](char value) -> char { return value; });
  SECTION("Huffman")
  {
    auto archive = dacoexec::huffman::compress(input, { .block_size_ = partial_block_size });
    auto decoded = dacoexec::huffman::decompress(archive);
    CHECK(decoded == std::vector<std::byte>(text.size(), std::byte{ 'a' }));
    std::ostringstream stream;
    auto statistics = dacoexec::huffman::decompress_stream(archive, stream);
    CHECK(stream.str() == text);
    CHECK(statistics.frames_ == 9);
    CHECK(statistics.input_bytes_ == archive.size());
    CHECK(statistics.output_bytes_ == text.size());
    CHECK_THROWS_AS(dacoexec::huffman::decompress(archive, { .max_output_bytes_ = 100 }), dacoexec::exception);
    archive.pop_back();
    CHECK_THROWS_AS(dacoexec::huffman::decompress(archive), dacoexec::exception);
  }
  SECTION("ANS")
  {
    std::vector<std::byte> archive;
    auto statistics =
      dacoexec::ans::compress_stream(input, std::back_inserter(archive), { .block_size_ = partial_block_size });
    CHECK(statistics.frames_ == 9);
    CHECK(statistics.input_bytes_ == text.size());
    CHECK(statistics.output_bytes_ == archive.size());
    auto decoded = dacoexec::ans::decompress(archive);
    CHECK(decoded == std::vector<std::byte>(text.size(), std::byte{ 'a' }));
    CHECK_THROWS_AS(dacoexec::ans::decompress(archive, { .max_output_bytes_ = 100 }), dacoexec::exception);
    archive.push_back(std::byte{ 0 });
    CHECK_THROWS_AS(dacoexec::ans::decompress(archive), dacoexec::exception);
  }
}

TEST_CASE("Public codecs handle empty input and validate options before sink writes", "[facade]")
{
  std::vector<std::byte> empty;
  CHECK(dacoexec::huffman::decompress(dacoexec::huffman::compress(empty)).empty());
  CHECK(dacoexec::ans::decompress(dacoexec::ans::compress(empty)).empty());
  std::ostringstream sink;
  CHECK_THROWS_AS(dacoexec::huffman::compress_stream(empty, sink, { .block_size_ = 0 }), dacoexec::exception);
  CHECK(sink.str().empty());
  CHECK_THROWS_AS(dacoexec::ans::compress_stream(empty, sink, { .block_size_ = 0 }), dacoexec::exception);
  CHECK(sink.str().empty());
  CHECK_THROWS_AS(dacoexec::huffman::decompress(empty), dacoexec::exception);
  CHECK_THROWS_AS(dacoexec::ans::decompress(empty), dacoexec::exception);
}

TEST_CASE("Public integer adaptors and bit iterators compose without integer primitives", "[facade]")
{
  std::array<std::uint64_t, 4> values{ 0, 1, sample_integer, long_unary_integer };
  auto encoded = values | dacoexec::views::encode::rice(3);
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  std::ranges::copy(encoded, dacoexec::bit_output_iterator{ writer });
  auto padding = writer.finish();
  auto bits = bytes | dacoexec::views::bits() | std::views::take((bytes.size() * byte_bits) - padding);
  auto decoded = std::move(bits) | dacoexec::views::decode::rice(3, values.size(), { .max_unary_bits_ = unary_limit });
  CHECK(std::ranges::equal(decoded, values));
  dacoexec::bit_reader const reader(bytes, (bytes.size() * byte_bits) - padding);
  CHECK(reader.remaining_bits() > 0);
}
