#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/views.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace {
using bytes = std::vector<std::byte>;
namespace arithmetic = dacoexec::arithmetic;
using dacoexec::test::error_is;

struct sink
{
  bytes bytes_;

  void write(std::span<std::byte const> input) { bytes_.insert(bytes_.end(), input.begin(), input.end()); }
};

template<class R>
concept byte_source = requires(R &&source) { arithmetic::compress(std::forward<R>(source)); };
static_assert(byte_source<std::vector<char> &>);
static_assert(byte_source<std::vector<signed char> &>);
static_assert(byte_source<std::vector<unsigned char> &>);
static_assert(byte_source<std::vector<char8_t> &>);
static_assert(!byte_source<std::vector<int> &>);
static_assert(!byte_source<std::vector<bool> &>);
}// namespace

TEST_CASE("Arithmetic facade streams mixed frames and reports public statistics", "[arithmetic][facade]")
{
  for (auto size : { 0U, 1U, 4096U, 8199U }) {
    bytes input(size, std::byte{ 'A' });
    for (std::size_t i = 4096; i < std::min(input.size(), std::size_t{ 8192 }); ++i) {
      input[i] = static_cast<std::byte>(i % 256);
    }
    auto archive = arithmetic::compress(input, { .block_size_ = 4096 });
    CHECK(arithmetic::decompress(archive) == input);
    sink encoded;
    auto encoding = arithmetic::compress_stream(input, encoded, { .block_size_ = 4096 });
    CHECK(encoded.bytes_ == archive);
    CHECK(encoding.input_bytes_ == input.size());
    CHECK(encoding.output_bytes_ == archive.size());
    CHECK(encoding.frames_ == (size + 4095) / 4096);
    bytes output;
    auto decoding = arithmetic::decompress_stream(archive, std::back_inserter(output));
    CHECK(output == input);
    CHECK(decoding.input_bytes_ == archive.size());
    CHECK(decoding.output_bytes_ == input.size());
    CHECK(decoding.frames_ == encoding.frames_);
  }
  std::string const text = "single pass byte input";
  std::istringstream source(text);
  auto archive = arithmetic::compress(
    std::ranges::subrange(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()));
  std::ostringstream output;
  static_cast<void>(arithmetic::decompress_stream(archive, output));
  CHECK(output.str() == text);
}

TEST_CASE("Arithmetic public entry points validate options and archives", "[arithmetic][facade]")
{
  bytes const input(4096, std::byte{ 'A' });
  auto archive = arithmetic::compress(input);
  for (auto coder : { arithmetic::entropy_coder::binary,
         arithmetic::entropy_coder::context_binary,
         static_cast<arithmetic::entropy_coder>(255) }) {
    sink output;
    REQUIRE_THROWS_MATCHES(arithmetic::compress_stream(bytes{}, output, { .coder_ = coder }),
      dacoexec::exception,
      error_is(dacoexec::errc::unsupported_coder));
    CHECK(output.bytes_.empty());
    REQUIRE_THROWS_MATCHES(dacoexec::views::encode::arithmetic({ .coder_ = coder }),
      dacoexec::exception,
      error_is(dacoexec::errc::unsupported_coder));
  }
  REQUIRE_THROWS_AS(arithmetic::compress(input, { .block_size_ = 0 }), dacoexec::exception);
  REQUIRE_THROWS_AS(arithmetic::decompress(archive, { .max_output_bytes_ = 4095 }), dacoexec::exception);
  REQUIRE_THROWS_AS(arithmetic::decompress(archive, { .max_frame_size_ = 4095 }), dacoexec::exception);
  archive.pop_back();
  REQUIRE_THROWS_AS(arithmetic::decompress(archive), dacoexec::exception);
}

TEST_CASE("Arithmetic views match streaming bytes and defer source consumption", "[arithmetic][views]")
{
  bytes input(8199, std::byte{ 'A' });
  std::size_t reads = 0;
  auto source = input | std::views::transform([&](std::byte value) {
    ++reads;
    return value;
  });
  auto encoded = source | dacoexec::views::encode::arithmetic({ .block_size_ = 4096 });
  auto cursor = encoded.begin();
  CHECK(reads == 0);
  bytes archive;
  std::ranges::copy(std::ranges::subrange(std::move(cursor), encoded.end()), std::back_inserter(archive));
  CHECK(reads == input.size());
  CHECK(archive == arithmetic::compress(input, { .block_size_ = 4096 }));
  auto decoded = bytes(archive) | dacoexec::views::decode::arithmetic();
  bytes output;
  std::ranges::copy(decoded, std::back_inserter(output));
  CHECK(output == input);
  auto limited = archive | dacoexec::views::decode::arithmetic({ .max_output_bytes_ = 4096 });
  output.clear();
  REQUIRE_THROWS_AS(std::ranges::copy(limited, std::back_inserter(output)), dacoexec::exception);
  CHECK(output == bytes(4096, std::byte{ 'A' }));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
