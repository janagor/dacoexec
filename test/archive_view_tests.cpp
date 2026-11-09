#include <catch2/catch_test_macros.hpp>
#include <dacoexec/views.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <dacoexec/ans.hpp>
#include <dacoexec/error.hpp>
#include <dacoexec/huffman.hpp>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace dv = dacoexec::views;
constexpr std::size_t block_size = 7;
constexpr std::size_t archive_header_size = 8;
constexpr std::size_t frame_header_size = 13;
constexpr std::size_t crc_offset = 9;
constexpr std::size_t alphabet_size = 256;
constexpr std::size_t sample_size = 20;
constexpr std::size_t lazy_sample_size = 30;
constexpr std::size_t compressed_block_size = 1024;
constexpr std::size_t compressed_sample_size = 2048;
constexpr std::uint64_t unlimited = UINT64_MAX;

template<std::ranges::input_range R> auto collect(R &&range) -> std::vector<std::byte>
{
  std::vector<std::byte> output;
  std::ranges::copy(std::forward<R>(range), std::back_inserter(output));
  return output;
}

template<class Function> auto with_codecs(Function function, std::size_t selected_block_size = block_size) -> void
{
  function(
    dv::encode::huffman({ .block_size_ = selected_block_size }),
    [](std::uint64_t limit = unlimited) -> auto { return dv::decode::huffman({ .max_output_bytes_ = limit }); },
    [selected_block_size](
      auto &source) -> auto { return dacoexec::huffman::compress(source, { .block_size_ = selected_block_size }); });
  for (auto coder : { dacoexec::ans::entropy_coder::rans, dacoexec::ans::entropy_coder::tans }) {
    function(
      dv::encode::ans({ .block_size_ = selected_block_size, .coder_ = coder }),
      [](std::uint64_t limit = unlimited) -> auto { return dv::decode::ans({ .max_output_bytes_ = limit }); },
      [coder, selected_block_size](auto &source) -> auto {
        return dacoexec::ans::compress(source, { .block_size_ = selected_block_size, .coder_ = coder });
      });
  }
}

template<class R>
concept archive_source = requires(R &&source) {
  dv::encode::huffman()(std::forward<R>(source));
  dv::decode::huffman()(std::forward<R>(source));
  dv::encode::ans()(std::forward<R>(source));
  dv::decode::ans()(std::forward<R>(source));
};
static_assert(archive_source<std::vector<std::byte> &>);
static_assert(archive_source<std::vector<char> &>);
static_assert(archive_source<std::vector<signed char> &>);
static_assert(archive_source<std::vector<unsigned char> &>);
static_assert(archive_source<std::vector<char8_t> &>);
static_assert(!archive_source<std::vector<int> &>);
static_assert(!archive_source<std::vector<bool> &>);
static_assert(!archive_source<std::vector<char16_t> &>);
static_assert(!archive_source<int>);

// Unsized, non-common input range with observable cursor acquisition and advancement.
class observed_view : public std::ranges::view_interface<observed_view>
{
public:
  struct counters
  {
    std::size_t begins_ = 0;
    std::size_t reads_ = 0;
    std::size_t advances_ = 0;
    std::size_t fail_at_ = SIZE_MAX;
    bool fail_begin_ = false;
    bool fail_increment_ = false;
    bool fail_compare_ = false;
  };

  observed_view(std::string const &source, counters &counts) : source_(&source), counts_(&counts) {}

  observed_view(observed_view const &) = delete;
  auto operator=(observed_view const &) -> observed_view & = delete;
  ~observed_view() = default;
  observed_view(observed_view &&) = default;
  auto operator=(observed_view &&) -> observed_view & = default;

  struct character_proxy
  {
    char value_;

    operator char() const { return value_; }// NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
  };

  struct iterator
  {
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;
    std::string const *source_;
    counters *counts_;
    std::size_t index_ = 0;

    auto operator*() const -> character_proxy
    {
      if (index_ == counts_->fail_at_) { throw std::runtime_error("source failure"); }
      ++counts_->reads_;
      return { source_->at(index_) };
    }

    auto operator++() -> iterator &
    {
      if (counts_->fail_increment_) { throw std::runtime_error("increment failure"); }
      ++counts_->advances_;
      ++index_;
      return *this;
    }

    auto operator++(int) -> void { ++*this; }

    friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel) -> bool
    {
      if (cursor.counts_->fail_compare_) { throw std::runtime_error("comparison failure"); }
      return cursor.index_ == cursor.source_->size();
    }
  };

  auto begin() -> iterator
  {
    if (counts_->fail_begin_) { throw std::runtime_error("begin failure"); }
    ++counts_->begins_;
    return { .source_ = source_, .counts_ = counts_ };
  }

  // cppcheck-suppress functionStatic
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] auto end() const -> std::default_sentinel_t { return {}; }

private:
  std::string const *source_;
  counters *counts_;
};
}// namespace

TEST_CASE("Archive views preserve archive bytes and compose", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    for (std::size_t const size : { std::size_t{ 0 },
           std::size_t{ 1 },
           block_size,
           block_size + 1,
           block_size * 2,
           lazy_sample_size + 1,
           compressed_sample_size }) {
      for (bool const varied : { false, true }) {
        std::string input(size, 'a');
        if (varied) {
          for (std::size_t index = 0; index < size; ++index) {
            input.at(index) = static_cast<char>(index % alphabet_size);
          }
        }
        auto archive = input | encode;
        using view_type = decltype(archive);
        STATIC_REQUIRE(std::ranges::view<view_type>);
        STATIC_REQUIRE(std::ranges::input_range<view_type>);
        STATIC_REQUIRE_FALSE(std::ranges::forward_range<view_type>);
        STATIC_REQUIRE_FALSE(std::ranges::sized_range<view_type>);
        STATIC_REQUIRE_FALSE(std::ranges::borrowed_range<view_type>);
        STATIC_REQUIRE_FALSE(std::ranges::range<view_type const>);
        STATIC_REQUIRE(std::same_as<std::ranges::range_value_t<view_type>, std::byte>);
        STATIC_REQUIRE(std::same_as<std::ranges::range_reference_t<view_type>, std::byte>);
        auto expected = compress(input);
        CHECK(collect(archive) == expected);
        auto decoded = expected | make_decode();
        STATIC_REQUIRE(std::ranges::input_range<decltype(decoded)>);
        auto plain = collect(decoded);
        CHECK(std::ranges::equal(
          plain,
          input,
          {},
          [](std::byte value) -> auto { return std::to_integer<unsigned char>(value); },
          [](char value) -> auto { return static_cast<unsigned char>(value); }));
        CHECK(collect(input | encode | make_decode()) == plain);
        CHECK(collect(std::string(input) | encode | make_decode() | std::views::drop(0)) == plain);
      }
    }
  });
}

TEST_CASE("Archive encoding views read only when a frame is needed", "[archive_views]")
{
  with_codecs([](auto encode, auto, auto compress) -> void {
    std::string input(lazy_sample_size, 'x');
    observed_view::counters counts;
    auto view = encode(observed_view{ input, counts });
    CHECK(counts.begins_ == 0);
    auto cursor = view.begin();
    CHECK(counts.begins_ == 0);
    auto copy = cursor;
    auto byte = *cursor;
    CHECK(*copy == byte);
    CHECK(*view.begin() == byte);
    CHECK(counts.reads_ == 0);
    // Both current formats have an eight-byte archive header.
    for (unsigned index = 0; index < archive_header_size; ++index) { ++cursor; }
    CHECK(counts.begins_ == 0);
    CHECK(counts.reads_ == 0);
    CHECK(*copy == *cursor);
    CHECK(counts.begins_ == 1);
    CHECK(counts.reads_ == block_size);
    CHECK(counts.advances_ == block_size);
    auto moved = std::move(view);
    // Moved-from emptiness is part of the view contract.
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    CHECK(view.begin() == view.end());
    CHECK(*cursor == *moved.begin());
    auto expected = compress(input);
    auto remaining = collect(moved);
    CHECK(std::ranges::equal(remaining, expected | std::views::drop(archive_header_size)));
    CHECK(cursor == moved.end());
    CHECK(counts.begins_ == 1);
    CHECK(counts.reads_ == input.size());
  });
}

TEST_CASE("Archive views preserve cursors across moves and own temporary sources", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'z');
    auto archive = compress(input);
    auto decoded = std::vector<std::byte>(archive) | make_decode();
    auto cursor = decoded.begin();
    ++cursor;
    auto moved = std::move(decoded);
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    CHECK(decoded.begin() == decoded.end());
    CHECK(*cursor == std::byte{ 'z' });
    CHECK(collect(moved).size() == input.size() - 1);
    CHECK(cursor == moved.end());
    decltype(moved) empty;
    CHECK(empty.begin() == empty.end());
    auto encoded = std::string(input) | encode;
    auto owned = std::move(encoded);
    CHECK(collect(owned) == archive);
  });
}

TEST_CASE("Archive view failures are terminal and never synthesize END", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'x');
    observed_view::counters counts;
    counts.fail_at_ = block_size;
    auto encoded = encode(observed_view{ input, counts });
    std::vector<std::byte> prefix;
    CHECK_THROWS_AS(std::ranges::copy(encoded, std::back_inserter(prefix)), std::runtime_error);
    CHECK(encoded.begin() == encoded.end());
    CHECK(counts.reads_ == block_size);
    auto invalid = prefix | make_decode();
    CHECK_THROWS(collect(invalid));
    CHECK(invalid.begin() == invalid.end());
    auto archive = compress(input);
    archive.pop_back();
    auto truncated = archive | make_decode();
    CHECK_THROWS_AS(collect(truncated), dacoexec::format_error);
    CHECK(truncated.begin() == truncated.end());
  });
}

TEST_CASE("Archive decoding verifies whole frames and validates END on exhaustion", "[archive_views]")
{
  with_codecs([](auto, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'x');
    auto archive = compress(input);
    auto limited = archive | make_decode(block_size - 1);
    CHECK_THROWS_AS(*limited.begin(), dacoexec::format_error);
    CHECK(limited.begin() == limited.end());
    auto later_limit = archive | make_decode(block_size);
    std::vector<std::byte> output;
    CHECK_THROWS_AS(std::ranges::copy(later_limit, std::back_inserter(output)), dacoexec::format_error);
    CHECK(output == std::vector<std::byte>(block_size, std::byte{ 'x' }));
    CHECK_THROWS_AS(collect(archive | make_decode(0)), dacoexec::format_error);
    std::string empty;
    auto empty_archive = compress(empty);
    CHECK(collect(empty_archive | make_decode(0)).empty());
    archive.push_back(std::byte{ 0 });
    auto complete = archive | make_decode();
    CHECK_THROWS_AS(collect(complete), dacoexec::format_error);
    // A consumer that stops early does not inspect the malformed suffix.
    auto partial = archive | make_decode() | std::views::take(1);
    CHECK(collect(partial) == std::vector<std::byte>{ std::byte{ 'x' } });
    // Small blocks use RAW fallback. Corrupt the first frame's CRC field.
    auto corrupt = compress(input);
    corrupt.at(archive_header_size + crc_offset) ^= std::byte{ 1 };
    auto corrupt_view = corrupt | make_decode();
    CHECK_THROWS_AS(*corrupt_view.begin(), dacoexec::format_error);
    CHECK(corrupt_view.begin() == corrupt_view.end());
  });
}

TEST_CASE("Archive adaptor factories validate options eagerly", "[archive_views]")
{
  CHECK_THROWS_AS(dv::encode::huffman({ .block_size_ = 0 }), std::invalid_argument);
  CHECK_THROWS_AS(dv::encode::ans({ .block_size_ = 0 }), std::invalid_argument);
  // Invalid enum values must be rejected at the public boundary.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_THROWS_AS(dv::encode::ans({ .coder_ = static_cast<dacoexec::ans::entropy_coder>(99) }), std::invalid_argument);
  CHECK_THROWS_AS(
    dv::encode::ans({ .coder_ = dacoexec::ans::entropy_coder::tans, .table_log_ = 0 }), std::invalid_argument);
  CHECK_THROWS_AS(dv::decode::huffman({ .max_frame_size_ = 0 }), std::invalid_argument);
  CHECK_THROWS_AS(dv::decode::ans({ .max_frame_size_ = 0 }), std::invalid_argument);
}

TEST_CASE("Archive views handle compressed frames and reject every truncated prefix", "[archive_views]")
{
  with_codecs(
    [](auto encode, auto make_decode, auto compress) -> void {
      std::string input(compressed_sample_size, 'a');
      auto archive = compress(input);
      REQUIRE(archive.size() < input.size());
      CHECK(collect(input | encode) == archive);
      CHECK(collect(archive | make_decode()) == std::vector<std::byte>(input.size(), std::byte{ 'a' }));
      for (std::size_t length = 0; length < archive.size(); ++length) {
        auto prefix = archive | std::views::take(length);
        auto decoded = prefix | make_decode();
        CHECK_THROWS(collect(decoded));
        CHECK(decoded.begin() == decoded.end());
      }
      archive.front() = std::byte{ 0 };
      CHECK_THROWS_AS(collect(archive | make_decode()), dacoexec::format_error);
    },
    compressed_block_size);
}

TEST_CASE("Archive decoding publishes earlier frames but no bytes from a corrupt later frame", "[archive_views]")
{
  with_codecs([](auto, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'x');
    auto archive = compress(input);
    archive.at(archive_header_size + frame_header_size + block_size + crc_offset) ^= std::byte{ 1 };
    auto decoded = archive | make_decode();
    std::vector<std::byte> output;
    CHECK_THROWS_AS(std::ranges::copy(decoded, std::back_inserter(output)), dacoexec::format_error);
    CHECK(output == std::vector<std::byte>(block_size, std::byte{ 'x' }));
    CHECK(decoded.begin() == decoded.end());
  });
}

TEST_CASE("Archive decoding is lazy and destruction never drains input", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string input(lazy_sample_size, 'x');
    auto stored = compress(input);
    std::string archive;
    for (auto byte : stored) { archive.push_back(std::to_integer<char>(byte)); }
    observed_view::counters counts;
    {
      auto decoded = make_decode()(observed_view{ archive, counts });
      CHECK(counts.begins_ == 0);
      auto cursor = decoded.begin();
      STATIC_REQUIRE(noexcept(decoded.begin()));
      static_cast<void>(decoded.begin());
      CHECK(counts.begins_ == 0);
      CHECK(counts.reads_ == 0);
      CHECK(*cursor == std::byte{ 'x' });
      CHECK(counts.begins_ == 1);
      CHECK(counts.reads_ == archive_header_size + frame_header_size + block_size);
      auto const reads = counts.reads_;
      CHECK(*cursor == std::byte{ 'x' });
      CHECK(*decoded.begin() == *cursor);
      ++cursor;
      CHECK(counts.reads_ == reads);
    }
    CHECK(counts.reads_ == archive_header_size + frame_header_size + block_size);
    observed_view::counters encoding_counts;
    {
      auto encoded = encode(observed_view{ input, encoding_counts });
      static_cast<void>(encoded.begin());
    }
    CHECK(encoding_counts.begins_ == 0);
  });
}

TEST_CASE("Archive views terminate on source cursor failures", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string input(lazy_sample_size, 'x');
    auto stored = compress(input);
    std::string archive;
    for (auto byte : stored) { archive.push_back(std::to_integer<char>(byte)); }
    for (unsigned failure = 0; failure < 4; ++failure) {
      observed_view::counters counts;
      counts.fail_begin_ = failure == 0;
      counts.fail_compare_ = failure == 1;
      counts.fail_increment_ = failure == 2;
      counts.fail_at_ = failure == 3 ? 0 : SIZE_MAX;
      auto encoded = encode(observed_view{ input, counts });
      CHECK_THROWS_AS(collect(encoded), std::runtime_error);
      CHECK(encoded.begin() == encoded.end());
      auto decoded = make_decode()(observed_view{ archive, counts });
      CHECK_THROWS_AS(*decoded.begin(), std::runtime_error);
      CHECK(decoded.begin() == decoded.end());
    }
  });
}

TEST_CASE("Archive view decode limits reject oversized frames", "[archive_views]")
{
  std::string input(sample_size, 'x');
  auto huffman = dacoexec::huffman::compress(input);
  auto ans = dacoexec::ans::compress(input);
  CHECK_THROWS_AS(collect(huffman | dv::decode::huffman({ .max_frame_size_ = 10 })), dacoexec::format_error);
  CHECK_THROWS_AS(collect(ans | dv::decode::ans({ .max_frame_size_ = 10 })), dacoexec::format_error);
  constexpr std::size_t too_large = (16 * 1024 * 1024) + 1;
  CHECK_THROWS_AS(dv::encode::huffman({ .block_size_ = too_large }), std::invalid_argument);
  CHECK_THROWS_AS(dv::encode::ans({ .block_size_ = too_large }), std::invalid_argument);
  CHECK_THROWS_AS(dv::decode::huffman({ .max_frame_size_ = too_large }), std::invalid_argument);
  CHECK_THROWS_AS(dv::decode::ans({ .max_frame_size_ = too_large }), std::invalid_argument);
}

TEST_CASE("Taking exactly an archive header never acquires the encoder source", "[archive_views]")
{
  with_codecs([](auto encode, auto, auto compress) -> void {
    std::string input(sample_size, 'x');
    auto expected = compress(input);
    observed_view::counters counts;
    counts.fail_begin_ = true;
    auto prefix = encode(observed_view{ input, counts }) | std::views::take(archive_header_size);
    CHECK(collect(prefix) == collect(expected | std::views::take(archive_header_size)));
    CHECK(counts.begins_ == 0);
    CHECK(counts.reads_ == 0);
  });
}

TEST_CASE("Taking exactly a decoded frame leaves a corrupt next frame untouched", "[archive_views]")
{
  with_codecs([](auto, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'x');
    auto stored = compress(input);
    // Seven-byte blocks select RAW in all codecs. Corrupt the second frame's CRC.
    auto const first_frame_end = archive_header_size + frame_header_size + block_size;
    stored.at(first_frame_end + crc_offset) ^= std::byte{ 1 };
    std::string archive;
    for (auto byte : stored) { archive.push_back(std::to_integer<char>(byte)); }
    observed_view::counters counts;
    auto decoded = make_decode()(observed_view{ archive, counts });
    auto cursor = decoded.begin();
    auto prefix = std::move(decoded) | std::views::take(block_size);
    CHECK(collect(prefix) == std::vector<std::byte>(block_size, std::byte{ 'x' }));
    CHECK(counts.reads_ == first_frame_end);
    CHECK(counts.advances_ == first_frame_end);
    // Resuming through either dereference or end comparison must still verify it.
    CHECK_THROWS_AS(*cursor, dacoexec::format_error);
    CHECK(cursor == std::default_sentinel);
    CHECK(prefix.begin() == prefix.end());

    auto other = stored | make_decode();
    auto next = other.begin();
    for (std::size_t index = 0; index < block_size; ++index) { ++next; }
    CHECK_THROWS_AS(next == other.end(), dacoexec::format_error);
    CHECK(next == other.end());
  });
}

TEST_CASE("Archive views preserve donor iterators after move assignment during iteration", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string first_input(sample_size, 'x');
    std::string second_input(lazy_sample_size, 'y');
    auto first = first_input | encode;
    auto second = second_input | encode;
    auto donor_cursor = first.begin();
    // Leave the donor at the header boundary and the destination inside its header.
    for (std::size_t index = 0; index < archive_header_size; ++index) { ++donor_cursor; }
    auto replaced_cursor = second.begin();
    ++replaced_cursor;
    second = std::move(first);
    // The replaced cursor is invalidated and must not be used after assignment.
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    CHECK(first.begin() == first.end());
    CHECK(*donor_cursor == *second.begin());
    auto expected = compress(first_input);
    CHECK(collect(second) == collect(expected | std::views::drop(archive_header_size)));
    CHECK(donor_cursor == second.end());

    auto stored = compress(first_input);
    auto decoded_first = stored | make_decode();
    auto decoded_second = stored | make_decode();
    auto decoded_cursor = decoded_first.begin();
    for (std::size_t index = 0; index < block_size; ++index) { ++decoded_cursor; }
    CHECK(*decoded_second.begin() == std::byte{ 'x' });
    decoded_second = std::move(decoded_first);
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    CHECK(decoded_first.begin() == decoded_first.end());
    CHECK(*decoded_cursor == std::byte{ 'x' });
    CHECK(collect(decoded_second) == std::vector<std::byte>(sample_size - block_size, std::byte{ 'x' }));
    CHECK(decoded_cursor == decoded_second.end());
  });
}

TEST_CASE("Reused archive closures maintain independent passes", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string first_input(sample_size, 'x');
    std::string second_input(lazy_sample_size, 'y');
    auto first = first_input | encode;
    auto second = second_input | encode;
    auto first_cursor = first.begin();
    auto second_cursor = second.begin();
    ++first_cursor;
    ++second_cursor;
    ++second_cursor;
    auto first_archive = compress(first_input);
    auto second_archive = compress(second_input);
    CHECK(collect(first) == collect(first_archive | std::views::drop(1)));
    CHECK(collect(second) == collect(second_archive | std::views::drop(2)));

    auto decode = make_decode();
    auto first_decoded = first_archive | decode;
    auto second_decoded = second_archive | decode;
    auto first_plain = first_decoded.begin();
    auto second_plain = second_decoded.begin();
    ++first_plain;
    ++second_plain;
    ++second_plain;
    CHECK(collect(first_decoded) == std::vector<std::byte>(sample_size - 1, std::byte{ 'x' }));
    CHECK(collect(second_decoded) == std::vector<std::byte>(lazy_sample_size - 2, std::byte{ 'y' }));
  });
}

TEST_CASE("Archive pipelines preserve bytes across mismatched codec frame sizes", "[archive_views]")
{
  std::string input(compressed_sample_size, 'x');
  for (std::size_t index = 0; index < input.size(); ++index) {
    input.at(index) = static_cast<char>(index % alphabet_size);
  }
  auto plain = collect(input | std::views::transform([](char value) -> std::byte {
    return static_cast<std::byte>(static_cast<unsigned char>(value));
  }));
  for (auto coder : { dacoexec::ans::entropy_coder::rans, dacoexec::ans::entropy_coder::tans }) {
    auto pipeline = input | dv::encode::huffman({ .block_size_ = block_size }) | dv::decode::huffman()
                    | dv::encode::ans({ .block_size_ = compressed_block_size, .coder_ = coder }) | dv::decode::ans();
    CHECK(collect(pipeline) == plain);
    auto reverse = input | dv::encode::ans({ .block_size_ = compressed_block_size, .coder_ = coder })
                   | dv::decode::ans() | dv::encode::huffman({ .block_size_ = block_size }) | dv::decode::huffman();
    CHECK(collect(reverse) == plain);
  }
}

TEST_CASE("Taking zero archive bytes performs no producer work", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'x');
    observed_view::counters encoding_counts;
    encoding_counts.fail_begin_ = true;
    auto encoded = encode(observed_view{ input, encoding_counts });
    STATIC_REQUIRE(noexcept(encoded.begin()));
    auto encoded_cursor = encoded.begin();
    auto encoded_zero = std::move(encoded) | std::views::take(0);
    CHECK(collect(encoded_zero).empty());
    CHECK(encoding_counts.begins_ == 0);
    CHECK(encoding_counts.reads_ == 0);
    CHECK(encoding_counts.advances_ == 0);
    // A retained cursor can still request the header after taking zero bytes.
    CHECK(*encoded_cursor == std::byte{ 'D' });
    CHECK(encoding_counts.begins_ == 0);
    auto encoding_remainder = std::move(encoded_zero).base();
    CHECK_THROWS_AS(collect(encoding_remainder), std::runtime_error);
    CHECK(encoding_remainder.begin() == encoding_remainder.end());

    auto stored = compress(input);
    stored.at(archive_header_size + crc_offset) ^= std::byte{ 1 };
    std::string archive;
    for (auto byte : stored) { archive.push_back(std::to_integer<char>(byte)); }
    observed_view::counters decoding_counts;
    auto decoded = make_decode()(observed_view{ archive, decoding_counts });
    auto decoded_cursor = decoded.begin();
    auto decoded_zero = std::move(decoded) | std::views::take(0);
    CHECK(collect(decoded_zero).empty());
    CHECK(decoding_counts.begins_ == 0);
    CHECK(decoding_counts.reads_ == 0);
    CHECK(decoding_counts.advances_ == 0);
    // Zero consumption does not hide the error when a byte is later requested.
    CHECK_THROWS_AS(*decoded_cursor, dacoexec::format_error);
    CHECK(decoded_cursor == std::default_sentinel);

    observed_view::counters throwing_counts;
    throwing_counts.fail_begin_ = true;
    auto throwing = make_decode()(observed_view{ archive, throwing_counts });
    auto throwing_zero = std::move(throwing) | std::views::take(0);
    CHECK(collect(throwing_zero).empty());
    CHECK(throwing_counts.begins_ == 0);
    CHECK(throwing_counts.reads_ == 0);

    // Even an empty, invalid archive is not inspected by take(0).
    std::vector<std::byte> empty;
    CHECK(collect(empty | make_decode() | std::views::take(0)).empty());
  });
}

TEST_CASE("Archive cursors initialize on end comparison or increment when needed", "[archive_views]")
{
  with_codecs([](auto encode, auto make_decode, auto compress) -> void {
    std::string input(sample_size, 'x');
    auto stored = compress(input);
    std::string archive;
    for (auto byte : stored) { archive.push_back(std::to_integer<char>(byte)); }
    observed_view::counters counts;
    auto decoded = make_decode()(observed_view{ archive, counts });
    auto cursor = decoded.begin();
    CHECK(counts.begins_ == 0);
    CHECK(cursor != decoded.end());
    CHECK(counts.begins_ == 1);
    CHECK(counts.reads_ == archive_header_size + frame_header_size + block_size);
    CHECK(*cursor == std::byte{ 'x' });

    observed_view::counters skip_counts;
    auto skipped = make_decode()(observed_view{ archive, skip_counts });
    auto skipped_cursor = skipped.begin();
    CHECK(skip_counts.begins_ == 0);
    ++skipped_cursor;
    CHECK(skip_counts.begins_ == 1);
    CHECK(collect(skipped) == std::vector<std::byte>(sample_size - 1, std::byte{ 'x' }));

    std::vector<std::byte> empty;
    auto empty_archive = compress(empty);
    auto decoded_empty = empty_archive | make_decode();
    CHECK(decoded_empty.begin() == decoded_empty.end());
    auto encoded_empty = empty | encode;
    CHECK(encoded_empty.begin() != encoded_empty.end());
    CHECK(collect(encoded_empty) == empty_archive);
  });
}
