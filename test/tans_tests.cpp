#include "error_test_utils.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <dacoexec/ans.hpp>
#include <dacoexec/detail/ans/encoder.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/ans/model.hpp>
#include <dacoexec/detail/ans/stream.hpp>
#include <dacoexec/detail/ans/tans/codec.hpp>
#include <dacoexec/detail/ans/tans/table.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/support/endian.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

// Numeric literals below are wire fixtures, bounds and deliberate corruptions.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)


TEST_CASE("tANS transitions invert every state and present symbol", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  for (unsigned precision = 8; precision <= 12; ++precision) {
    ans::frequency_table counts{};
    for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) { counts.at(symbol) = symbol + 1; }
    auto model = ans::make_model(counts, precision);
    ans::tans::tables const table(model);
    for (unsigned symbol = 0; symbol < 256; ++symbol) {
      for (std::uint32_t state = 0; state < model.total(); ++state) {
        auto transition = table.encode(state, symbol);
        auto entry = table.decode(transition.state_);
        if (entry.symbol_ != symbol || entry.num_bits_ != transition.num_bits_
            || entry.baseline_ + transition.bits_ != state) {
          CAPTURE(precision, state, symbol);
          FAIL("tANS transition does not invert decoding");
        }
      }
    }
    SUCCEED("All states and symbols invert decoding at this precision");
  }
}

TEST_CASE("tANS standalone and archive round trips", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  std::mt19937 random(42);
  for (unsigned precision = 8; precision <= 12; ++precision) {
    for (unsigned distribution = 0; distribution < 4; ++distribution) {
      std::vector<std::byte> input(4096);
      for (auto &value : input) {
        constexpr std::array alphabets{ 1U, 2U, 17U, 256U };
        value = static_cast<std::byte>(random() % alphabets.at(distribution));
      }
      auto model = ans::make_model(ans::count_frequencies(input), precision);
      auto payload = ans::tans::encode_block(input, model);
      REQUIRE(ans::tans::decode_block(payload, model, input.size()) == input);
      REQUIRE(payload == ans::tans::encode_block(input, model));
      auto truncated = payload;
      truncated.pop_back();
      REQUIRE_THROWS_MATCHES(ans::tans::decode_block(truncated, model, input.size()),
        dacoexec::exception,
        dacoexec::test::error_is(dacoexec::errc::invalid_payload));
      payload.push_back(std::byte{ 0 });
      REQUIRE_THROWS_MATCHES(ans::tans::decode_block(payload, model, input.size()),
        dacoexec::exception,
        dacoexec::test::error_is(dacoexec::errc::invalid_payload));
      auto archive = dacoexec::ans::compress(
        input, { .block_size_ = 1024, .coder_ = dacoexec::ans::entropy_coder::tans, .table_log_ = precision });
      REQUIRE(dacoexec::ans::decompress(archive) == input);
    }
  }
  std::vector<std::byte> empty;
  ans::frequency_table counts{};
  counts.at(0) = 1;
  auto byte_model = ans::make_model(counts, 10);
  REQUIRE(ans::tans::decode_block(ans::tans::encode_block(empty, byte_model), byte_model, 0).empty());
  REQUIRE(dacoexec::ans::decompress(dacoexec::ans::compress(empty, { .coder_ = dacoexec::ans::entropy_coder::tans }))
      .empty());
  REQUIRE_THROWS_MATCHES(
    dacoexec::ans::compress(empty, { .coder_ = dacoexec::ans::entropy_coder::tans, .table_log_ = 7 }),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
}

TEST_CASE("tANS canonical payload and malformed state", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  ans::frequency_table counts{};
  counts.at(42) = 1;
  auto model = ans::make_model(counts, 10);
  std::vector<std::byte> input(3, std::byte{ 42 });
  auto payload = ans::tans::encode_block(input, model);
  REQUIRE(payload == std::vector<std::byte>(8, std::byte{ 0 }));
  payload.at(1) = std::byte{ 4 };
  REQUIRE_THROWS_MATCHES(ans::tans::decode_block(payload, model, input.size()),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  std::vector<std::byte> archive;
  REQUIRE_THROWS_MATCHES(
    dacoexec::ans::compress_stream(
      input, std::back_inserter(archive), { .coder_ = dacoexec::ans::entropy_coder::tans, .table_log_ = 13 }),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE(archive.empty());
}

TEST_CASE("tANS frames verify checksum padding and output bounds", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  std::vector<std::byte> input(4096, std::byte{ 7 });
  input.at(0) = std::byte{ 8 };
  auto model = ans::make_model(ans::count_frequencies(input), 10);
  auto payload = ans::tans::encode_block(input, model);
  auto bits = dacoexec::detail::support::read_u32_le(std::span<std::byte const>{ payload }.subspan<4, 4>());
  if (bits % 8U != 0) {
    auto damaged = payload;
    damaged.back() |= std::byte{ 1 };
    REQUIRE_THROWS_MATCHES(ans::tans::decode_block(damaged, model, input.size()),
      dacoexec::exception,
      dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  }
  REQUIRE_THROWS_MATCHES(ans::tans::decode_block(payload, model, input.size(), 1),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  auto archive = dacoexec::ans::compress(input, { .coder_ = dacoexec::ans::entropy_coder::tans });
  REQUIRE(archive.at(ans::archive_header_size) == std::byte{ 2 });
  REQUIRE_THROWS_MATCHES(dacoexec::ans::decompress(archive, { .max_output_bytes_ = 1 }),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  archive.at(ans::archive_header_size + ans::crc_field_offset) ^= std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(dacoexec::ans::decompress(archive),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::checksum_mismatch));
}

TEST_CASE("tANS two-symbol golden payload", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  ans::frequency_table counts{};
  counts.at(0) = 1;
  counts.at(1) = 1;
  auto model = ans::make_model(counts, 8);
  std::vector<std::byte> input{ std::byte{ 0 }, std::byte{ 1 }, std::byte{ 1 }, std::byte{ 0 }, std::byte{ 1 } };
  std::vector<std::byte> golden{ std::byte{ 9 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 5 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 0 },
    std::byte{ 192 } };
  REQUIRE(ans::tans::encode_block(input, model) == golden);
  REQUIRE(ans::tans::decode_block(golden, model, input.size()) == input);
}

namespace {
// Slow specification packer, retained to detect wire changes in optimized packing.
auto reference_payload(std::span<std::byte const> input, dacoexec::detail::ans::frequency_model<256> const &model)
  -> std::vector<std::byte>
{
  namespace ans = dacoexec::detail::ans;
  ans::tans::tables const table(model);
  std::vector<unsigned> bits;
  std::uint32_t state = 0;
  for (auto symbol : std::views::reverse(input)) {
    auto transition = table.encode(state, std::to_integer<unsigned>(symbol));
    for (unsigned bit = 0; bit < transition.num_bits_; ++bit) { bits.push_back((transition.bits_ >> bit) & 1U); }
    state = transition.state_;
  }
  std::vector<std::byte> result;
  for (auto field : { state, static_cast<std::uint32_t>(bits.size()) }) {
    auto bytes = dacoexec::detail::support::write_u32_le(field);
    result.insert(result.end(), bytes.begin(), bytes.end());
  }
  result.resize(8 + ((bits.size() + 7) / 8));
  for (std::size_t bit = 0; bit < bits.size(); ++bit) {
    result.at(8 + (bit / 8)) |= static_cast<std::byte>(bits.at(bits.size() - bit - 1) << (7 - (bit % 8)));
  }
  return result;
}
}// namespace

TEST_CASE("tANS randomized models and packing match the specification", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  std::mt19937 random(173);
  for (unsigned precision = 8; precision <= 12; ++precision) {
    for (unsigned const alphabet : { 1U, 2U, 17U, 256U }) {
      for (unsigned trial = 0; trial < 3; ++trial) {
        ans::frequency_table counts{};
        // Coprime permutation also exercises sparse noncontiguous alphabets.
        for (unsigned symbol = 0; symbol < alphabet; ++symbol) {
          counts.at(((symbol * 73U) + 19U) % 256U) = 1U + (random() % 100000U);
        }
        auto model = ans::make_model(counts, precision);
        ans::tans::tables const table(model);
        std::array<unsigned, 256> slots{};
        for (std::uint32_t state = 0; state < model.total(); ++state) { ++slots.at(table.decode(state).symbol_); }
        REQUIRE(slots == model.frequencies());
        for (unsigned symbol = 0; symbol < 256; ++symbol) {
          if (counts.at(symbol) == 0) {
            REQUIRE_THROWS_MATCHES(
              table.encode(0, symbol), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_argument));
            continue;
          }
          for (std::uint32_t state = 0; state < model.total(); ++state) {
            auto transition = table.encode(state, symbol);
            auto entry = table.decode(transition.state_);
            if (entry.symbol_ != symbol || entry.num_bits_ != transition.num_bits_
                || entry.baseline_ + transition.bits_ != state) {
              CAPTURE(precision, alphabet, trial, state, symbol);
              FAIL("Randomized tANS transition does not invert decoding");
            }
          }
        }
        for (std::size_t const size : { 0U, 1U, 7U, 8U, 9U, 31U, 257U }) {
          std::vector<std::byte> input(size);
          for (auto &value : input) { value = static_cast<std::byte>((((random() % alphabet) * 73U) + 19U) % 256U); }
          auto payload = ans::tans::encode_block(input, model);
          REQUIRE(payload == reference_payload(input, model));
          REQUIRE(ans::tans::decode_block(payload, model, input.size()) == input);
        }
      }
    }
  }
}

TEST_CASE("Unknown ANS coders fail before writing even an empty archive", "[ans][tans]")
{
  std::vector<std::byte> empty;
  std::vector<std::byte> destination;
  // Deliberately exercise an enum value outside the supported enumerators.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  auto invalid = static_cast<dacoexec::ans::entropy_coder>(99);
  REQUIRE_THROWS_MATCHES(dacoexec::ans::compress_stream(empty, std::back_inserter(destination), { .coder_ = invalid }),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::unsupported_coder));
  REQUIRE(destination.empty());
  REQUIRE_THROWS_MATCHES(dacoexec::detail::ans::encoder(invalid),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::unsupported_coder));
  REQUIRE_NOTHROW(dacoexec::ans::compress(empty, { .table_log_ = 0 }));
}

TEST_CASE("One DAN1 archive mixes RAW rANS and tANS", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  std::vector<std::byte> archive;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(archive) };
  ans::write_archive_header(sink);
  std::vector<std::byte> expected;
  for (auto coder : { ans::entropy_coder::rans, ans::entropy_coder::rans, ans::entropy_coder::tans }) {
    bool const raw = expected.empty();
    std::vector<std::byte> input(raw ? 1 : 4096, std::byte{ 42 });
    auto frame = ans::encode_frame({ 0, input }, coder);
    auto expected_kind = ans::frame_kind::raw;
    if (!raw) { expected_kind = coder == ans::entropy_coder::rans ? ans::frame_kind::rans : ans::frame_kind::tans; }
    REQUIRE(frame.header_.kind_ == expected_kind);
    ans::write_frame(sink, frame);
    expected.insert(expected.end(), input.begin(), input.end());
  }
  ans::write_frame_header(sink, {});
  REQUIRE(dacoexec::ans::decompress(archive) == expected);
  archive.push_back(std::byte{ 0 });
  REQUIRE_THROWS_MATCHES(
    dacoexec::ans::decompress(archive), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_archive));
}

TEST_CASE("tANS archive metadata rejects invalid precision counts and frequencies", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  std::vector<std::byte> input(4096, std::byte{ 42 });
  auto archive = dacoexec::ans::compress(input, { .coder_ = dacoexec::ans::entropy_coder::tans });
  auto const model_offset = ans::archive_header_size + ans::frame_header_size;
  for (auto offset : { model_offset, model_offset + 1, model_offset + 4 }) {
    auto damaged = archive;
    damaged.at(offset) = std::byte{ 0 };
    REQUIRE_THROWS(dacoexec::ans::decompress(damaged));
  }
  auto damaged = archive;
  damaged.resize(damaged.size() - 1);
  REQUIRE_THROWS_MATCHES(dacoexec::ans::decompress(damaged),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::truncated_archive));
}

TEST_CASE("tANS skewed golden vectors retain the original wire format", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  ans::frequency_table counts{};
  counts.at(0) = 1;
  counts.at(1) = 3;
  counts.at(2) = 5;
  counts.at(3) = 9;
  std::vector<std::byte> input;
  for (unsigned symbol : { 3U, 3U, 0U, 1U, 2U, 3U, 0U, 2U, 3U }) { input.push_back(static_cast<std::byte>(symbol)); }
  constexpr std::array logs{ 8U, 10U, 12U };
  constexpr std::array<std::array<unsigned, 11>, 3> vectors{ { { 89, 0, 0, 0, 18, 0, 0, 0, 229, 237, 128 },
    { 7, 2, 0, 0, 18, 0, 0, 0, 229, 203, 128 },
    { 103, 8, 0, 0, 18, 0, 0, 0, 131, 241, 128 } } };
  for (std::size_t index = 0; index < logs.size(); ++index) {
    auto model = ans::make_model(counts, logs.at(index));
    std::vector<std::byte> golden;
    for (auto value : vectors.at(index)) { golden.push_back(static_cast<std::byte>(value)); }
    REQUIRE(ans::tans::encode_block(input, model) == golden);
    REQUIRE(ans::tans::decode_block(golden, model, input.size()) == input);
  }
}

TEST_CASE("tANS rejects nonterminal states and excess valid bits", "[ans][tans]")
{
  namespace ans = dacoexec::detail::ans;
  ans::frequency_table counts{};
  counts.at(0) = 1;
  counts.at(1) = 1;
  auto model = ans::make_model(counts, 8);
  std::vector<std::byte> input{ std::byte{ 0 }, std::byte{ 1 } };
  auto payload = ans::tans::encode_block(input, model);
  auto damaged = payload;
  damaged.at(4) = std::byte{ 3 };// Extra valid bit, still within the same final byte.
  REQUIRE_THROWS_MATCHES(ans::tans::decode_block(damaged, model, 2),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  damaged = payload;
  damaged.at(4) = std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(ans::tans::decode_block(damaged, model, 2),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
  std::vector<std::byte> nonterminal(8, std::byte{ 0 });
  nonterminal.at(0) = std::byte{ 1 };
  REQUIRE_THROWS_MATCHES(ans::tans::decode_block(nonterminal, model, 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
