// Catch assertion branches exceed cppcheck's normal analysis budget.
// cppcheck-suppress-file normalCheckLevelMaxBranches
#include <algorithm>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/huffman/canonical.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/tree.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>
#include <dacoexec/huffman/decode_view.hpp>
#include <dacoexec/views/buffered_bytes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

namespace {
using dacoexec::huffman::build_code_lengths;
using dacoexec::huffman::canonical_codebook;
using dacoexec::huffman::code_length_table;
using dacoexec::huffman::count_frequencies;
using dacoexec::huffman::decode_error;
using dacoexec::huffman::make_canonical_codebook;
using dacoexec::huffman::max_canonical_bits;
namespace huffman_views = dacoexec::huffman::views;
constexpr std::uint64_t sample_bits = 15;
constexpr std::string_view sample = "AAAAABBCD";
constexpr unsigned golden_first = 0x05;
constexpr unsigned golden_second = 0x6E;

[[nodiscard]] auto sample_book() -> canonical_codebook
{ return make_canonical_codebook(build_code_lengths(count_frequencies(sample))); }

template<std::ranges::input_range R>
[[nodiscard]] auto collect(R &range) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  std::ranges::copy(range, std::back_inserter(result));
  return result;
}

struct source_error : std::runtime_error
{
  source_error() : std::runtime_error("source failure") {}
};
}// namespace

// Catch assertions expand into branches in this iterator lifecycle scenario.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Lazy decoder stages one symbol and transfers iteration state", "[huffman][decode]")
{
  unsigned reads = 0;
  auto source = std::vector<unsigned char>{ golden_first, golden_second }
    | std::views::transform([&reads](unsigned char value) -> unsigned char { ++reads; return value; });
  auto decoded = std::move(source) | huffman_views::decode(sample_book(), sample_bits, sample.size());
  CHECK(reads == 0);
  auto cursor = decoded.begin();
  REQUIRE(reads == 1);
  CHECK(*cursor == std::byte{ 'A' });
  auto alias = cursor;
  ++cursor;
  CHECK(*alias == *cursor);
  auto moved = std::move(decoded);
  // A moved-from decoder is specified to be empty.
  // cppcheck-suppress accessMoved
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  CHECK(decoded.begin() == decoded.end());
  CHECK(*moved.begin() == *cursor);
  std::vector<std::byte> result{ std::byte{ 'A' } };
  for (; cursor != moved.end(); ++cursor) { result.push_back(*cursor); }
  REQUIRE(result.size() == sample.size());
  for (std::size_t index = 0; index < sample.size(); ++index) {
    CHECK(result.at(index) == static_cast<std::byte>(sample.at(index)));
  }
  CHECK(reads == 2);
}

TEST_CASE("Decoder accepts a move-only single-pass source and owns its codebook", "[huffman][decode]")
{
  constexpr std::size_t window = 1;
  auto decoded = (std::vector<std::byte>{ std::byte{ golden_first }, std::byte{ golden_second } }
      | dacoexec::views::buffered_bytes(window))
    | huffman_views::decode(sample_book(), sample_bits, sample.size());
  CHECK(collect(decoded).size() == sample.size());
  auto source = std::vector<char>{ 0 };
  auto book = make_canonical_codebook(build_code_lengths(count_frequencies(std::string_view{ "A" })));
  auto independent = source | huffman_views::decode(book, 1, 1);
  book = sample_book();
  CHECK(collect(independent) == std::vector<std::byte>{ std::byte{ 'A' } });
}

TEST_CASE("Decoder propagates source failures and permanently ends", "[huffman][decode]")
{
  auto source = std::vector<std::byte>{ std::byte{ golden_first }, std::byte{ golden_second } }
    | std::views::transform([](std::byte value) -> std::byte {
        if (value == std::byte{ golden_second }) { throw source_error{}; }
        return value;
      });
  auto decoded = std::move(source) | huffman_views::decode(sample_book(), sample_bits, sample.size());
  auto cursor = decoded.begin();
  CHECK_THROWS_AS(collect(decoded), source_error);
  CHECK(cursor == decoded.end());
  CHECK(decoded.begin() == decoded.end());
  ++cursor;
  CHECK(cursor == decoded.end());
}

TEST_CASE("Decoder failure on begin is permanent", "[huffman][decode]")
{
  std::vector<std::byte> source;
  auto decoded = std::move(source) | huffman_views::decode(sample_book(), sample_bits, sample.size());
  CHECK_THROWS_AS(decoded.begin(), decode_error);
  CHECK(decoded.begin() == decoded.end());
}

TEST_CASE("Decoder handles maximum-width canonical codewords", "[huffman][decode]")
{
  code_length_table lengths{};
  for (unsigned length = 1; length < max_canonical_bits; ++length) {
    lengths.at(length - 1U) = static_cast<std::uint8_t>(length);
  }
  lengths.at(max_canonical_bits - 1U) = max_canonical_bits;
  lengths.at(max_canonical_bits) = max_canonical_bits;
  constexpr auto payload_size = max_canonical_bits / dacoexec::detail::bits_per_byte;
  std::vector<std::byte> source(payload_size, static_cast<std::byte>(std::numeric_limits<std::uint8_t>::max()));
  auto decoded = source | huffman_views::decode(make_canonical_codebook(lengths), max_canonical_bits, 1);
  CHECK(collect(decoded) == std::vector<std::byte>{ static_cast<std::byte>(max_canonical_bits) });
}

TEST_CASE("Empty lazy decoding requires an empty source", "[huffman][decode]")
{
  std::vector<std::byte> source;
  auto decoded = source | huffman_views::decode(make_canonical_codebook({}), 0, 0);
  CHECK(decoded.begin() == decoded.end());
  source.push_back(std::byte{ 0 });
  auto extra = source | huffman_views::decode(sample_book(), 0, 0);
  CHECK_THROWS_AS(extra.begin(), decode_error);
}

TEST_CASE("Decoder move assignment preserves existing source iterators", "[huffman][decode]")
{
  auto make_source = []() -> std::vector<std::byte> {
    return { std::byte{ golden_first }, std::byte{ golden_second } };
  };
  auto source = make_source() | huffman_views::decode(sample_book(), sample_bits, sample.size());
  auto destination = make_source() | huffman_views::decode(sample_book(), sample_bits, sample.size());
  auto cursor = source.begin();
  ++cursor;
  destination = std::move(source);
  // A moved-from decoder is specified to be empty.
  // cppcheck-suppress accessMoved
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
  CHECK(source.begin() == source.end());
  CHECK(*cursor == *destination.begin());
  CHECK(collect(destination).size() == sample.size() - 1U);
}

TEST_CASE("Decoder propagates source initialization and advance failures", "[huffman][decode]")
{
  struct failing_source : std::ranges::view_base
  {
    bool fail_begin_ = false;
    bool fail_advance_ = false;

    struct iterator
    {
      using value_type = std::byte;
      using difference_type = std::ptrdiff_t;
      using iterator_concept = std::input_iterator_tag;
      failing_source *owner_ = nullptr;
      bool ended_ = false;

      auto operator*() const -> std::byte { return std::byte{ 0 }; }
      auto operator++() -> iterator &
      {
        if (owner_->fail_advance_) { throw source_error{}; }
        ended_ = true;
        return *this;
      }
      auto operator++(int) -> void { ++*this; }
      auto operator==([[maybe_unused]] std::default_sentinel_t sentinel) const -> bool { return ended_; }
    };

    auto begin() -> iterator
    {
      if (fail_begin_) { throw source_error{}; }
      return { .owner_ = this, .ended_ = false };
    }
    [[nodiscard]] static auto end() -> std::default_sentinel_t { return {}; }
  };

  failing_source source;
  SECTION("begin") { source.fail_begin_ = true; }
  SECTION("increment") { source.fail_advance_ = true; }
  auto const book = make_canonical_codebook(build_code_lengths(count_frequencies(std::string_view{ "A" })));
  auto decoded = source | huffman_views::decode(book, dacoexec::detail::bits_per_byte, dacoexec::detail::bits_per_byte);
  CHECK_THROWS_AS(collect(decoded), source_error);
  CHECK(decoded.begin() == decoded.end());
}
