#include "../fuzz_test/error_policy.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/ans.hpp>
#include <dacoexec/bit.hpp>
#include <dacoexec/detail/ans/block.hpp>
#include <dacoexec/detail/codec/stream_statistics.hpp>
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/detail/integer_coding/golomb.hpp>
#include <dacoexec/error.hpp>
#include <dacoexec/huffman.hpp>
#include <dacoexec/views.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

auto error_category_from_other_translation_unit() noexcept -> std::error_category const &;

namespace {
using dacoexec::errc;
using bytes = std::vector<std::byte>;

template<class Function> auto check_error(errc expected, Function &&operation) -> void
{
  try {
    std::forward<Function>(operation)();
    FAIL("Expected a dacoexec error");
  } catch (dacoexec::exception const &error) {
    CHECK(error.code() == expected);
    CHECK(&error.code().category() == &dacoexec::category());
  }
}

struct foreign_error : std::runtime_error
{
  foreign_error() : std::runtime_error("client failure") {}
};
}// namespace

TEST_CASE("Structured error values and messages are stable", "[error]")
{
  constexpr std::array codes{ errc::invalid_argument,
    errc::invalid_state,
    errc::invalid_archive,
    errc::truncated_archive,
    errc::invalid_frame,
    errc::invalid_payload,
    errc::checksum_mismatch,
    errc::output_limit_exceeded,
    errc::numeric_overflow,
    errc::io_error,
    errc::unsupported_coder,
    errc::capacity_exceeded,
    errc::truncated_input,
    errc::codeword_limit_exceeded };
  constexpr std::array messages{ "Invalid argument",
    "Invalid state",
    "Invalid archive",
    "Truncated archive",
    "Invalid frame",
    "Invalid payload",
    "Checksum mismatch",
    "Output limit exceeded",
    "Numeric overflow",
    "I/O error",
    "Unsupported coder",
    "Capacity exceeded",
    "Truncated input",
    "Codeword limit exceeded" };
  CHECK(std::string(dacoexec::category().name()) == "dacoexec");
  CHECK(&dacoexec::category() == &error_category_from_other_translation_unit());
  for (std::size_t index = 0; index < codes.size(); ++index) {
    std::error_code const code = codes[index];
    CHECK(code.value() == static_cast<int>(index + 1));
    CHECK(code == codes[index]);
    CHECK(codes[index] == code);
    CHECK(code.message() == messages[index]);
    CHECK(code != std::make_error_code(std::errc::invalid_argument));
    dacoexec::exception const plain(codes[index]);
    CHECK(plain.code() == code);
    CHECK(std::string(plain.what()).find(messages[index]) != std::string::npos);
    try {
      throw dacoexec::exception(codes[index], "context");
    } catch (std::system_error const &error) {
      CHECK(error.code() == code);
      CHECK(std::string(error.what()).find("context") != std::string::npos);
    }
  }
  CHECK(dacoexec::category().message(0) == "Unknown dacoexec error");
  CHECK(dacoexec::category().message(-1) == "Unknown dacoexec error");
  CHECK(dacoexec::category().message(999) == "Unknown dacoexec error");
}

TEST_CASE("Archive errors distinguish framing checksum truncation and limits", "[error]")
{
  bytes const input{ std::byte{ 42 }, std::byte{ 43 } };
  for (bool ans : { false, true }) {
    auto archive = ans ? dacoexec::ans::compress(input, { .block_size_ = 1 })
                       : dacoexec::huffman::compress(input, { .block_size_ = 1 });
    auto decode = [&](bytes const &data, bytes &output, std::uint64_t limit = 100) {
      if (ans) {
        static_cast<void>(
          dacoexec::ans::decompress_stream(data, std::back_inserter(output), { .max_output_bytes_ = limit }));
      } else {
        static_cast<void>(
          dacoexec::huffman::decompress_stream(data, std::back_inserter(output), { .max_output_bytes_ = limit }));
      }
    };
    bytes output;
    auto damaged = archive;
    damaged[0] = std::byte{ 0 };
    check_error(errc::invalid_archive, [&] { decode(damaged, output); });
    CHECK(output.empty());
    damaged = archive;
    damaged.resize(1);
    check_error(errc::truncated_archive, [&] { decode(damaged, output); });
    CHECK(output.empty());
    auto const archive_header =
      ans ? dacoexec::detail::ans::archive_header_size : dacoexec::detail::huffman::archive_header_size;
    auto const frame_header =
      ans ? dacoexec::detail::ans::frame_header_size : dacoexec::detail::huffman::frame_header_size;
    damaged = archive;
    damaged[archive_header] = std::byte{ 255 };
    check_error(errc::invalid_frame, [&] { decode(damaged, output); });
    CHECK(output.empty());
    damaged = archive;
    // block_size_ = 1 makes each fixture frame RAW, with a one-byte payload.
    auto const second_frame = archive_header + frame_header + sizeof(input.front());
    damaged.at(second_frame + dacoexec::detail::codec::crc_field_offset) ^= std::byte{ 1 };
    check_error(errc::checksum_mismatch, [&] { decode(damaged, output); });
    CHECK(output == bytes{ input[0] });
    output.clear();
    check_error(errc::output_limit_exceeded, [&] { decode(archive, output, 1); });
    CHECK(output == bytes{ input[0] });
    output.clear();
    damaged = archive;
    damaged.push_back(std::byte{ 0 });
    check_error(errc::invalid_archive, [&] { decode(damaged, output); });
    CHECK(output == input);
  }
}

TEST_CASE("Invalid options fail before publishing archive headers", "[error]")
{
  bytes const input{ std::byte{ 0 } };
  bytes output;
  check_error(errc::invalid_argument, [&] {
    static_cast<void>(dacoexec::huffman::compress_stream(input, std::back_inserter(output), { .block_size_ = 0 }));
  });
  CHECK(output.empty());
  check_error(errc::unsupported_coder, [&] {
    static_cast<void>(dacoexec::ans::compress_stream(
      input, std::back_inserter(output), { .coder_ = static_cast<dacoexec::ans::entropy_coder>(255) }));
  });
  CHECK(output.empty());
  check_error(errc::invalid_argument, [&] {
    static_cast<void>(dacoexec::ans::compress_stream(
      input, std::back_inserter(output), { .coder_ = dacoexec::ans::entropy_coder::tans, .table_log_ = 0 }));
  });
  CHECK(output.empty());
}

TEST_CASE("Malformed codec blocks have payload errors", "[error]")
{
  dacoexec::detail::huffman::encoded_block huffman;
  huffman.uncompressed_size_ = 1;
  check_error(errc::invalid_payload,
    [&] { static_cast<void>(dacoexec::detail::huffman::decompress_block(std::move(huffman), 1)); });
  dacoexec::detail::ans::encoded_block ans;
  ans.uncompressed_size_ = 1;
  check_error(
    errc::invalid_payload, [&] { static_cast<void>(dacoexec::detail::ans::decompress_block(std::move(ans), 1)); });
}

TEST_CASE("Bit states limits overflow and stream failures carry error codes", "[error]")
{
  bytes output;
  dacoexec::bit_writer writer(std::back_inserter(output));
  check_error(errc::invalid_argument, [&] { writer.write_bits({ .value_ = 0, .bit_count_ = 33 }); });
  writer.write_bit(true);
  static_cast<void>(writer.finish());
  check_error(errc::invalid_state, [&] { writer.write_bit(false); });
  dacoexec::bit_reader reader(output, 1);
  CHECK(reader.read_bit());
  check_error(errc::truncated_input, [&] { static_cast<void>(reader.read_bit()); });
  auto counter = std::numeric_limits<std::uint64_t>::max();
  check_error(errc::numeric_overflow, [&] { dacoexec::detail::codec::checked_add(counter, 1); });
  CHECK(counter == std::numeric_limits<std::uint64_t>::max());
  std::ostringstream stream;
  stream.setstate(std::ios::badbit);
  check_error(errc::io_error, [&] { static_cast<void>(dacoexec::huffman::compress_stream(output, stream)); });
}

TEST_CASE("Client exceptions pass through library boundaries unchanged", "[error]")
{
  bytes const input{ std::byte{ 0 } };
  auto source = input | std::views::transform([](auto) -> std::byte { throw foreign_error{}; });
  CHECK_THROWS_AS(dacoexec::huffman::compress(source), foreign_error);

  struct sink
  {
    auto write(std::span<std::byte const>) -> void { throw foreign_error{}; }
  } destination;

  CHECK_THROWS_AS(dacoexec::ans::compress_stream(input, destination), foreign_error);
  // Even a client exception in the dacoexec category must retain its own code.
  auto library_error_source = input | std::views::transform([](auto) -> std::byte {
    throw dacoexec::exception(errc::numeric_overflow, "client error");
  });
  check_error(errc::numeric_overflow, [&] { static_cast<void>(dacoexec::ans::compress(library_error_source)); });
}

TEST_CASE("Archive metadata validators translate internal argument errors", "[error]")
{
  constexpr auto singleton_symbol = std::byte{ 42 };
  constexpr auto symbol_index = std::to_integer<std::size_t>(singleton_symbol);
  bytes const input(4096, singleton_symbol);
  auto huffman = dacoexec::huffman::compress(input);
  constexpr auto metadata =
    dacoexec::detail::huffman::archive_header_size + dacoexec::detail::huffman::frame_header_size;
  // A singleton Huffman code must have length one.
  huffman.at(metadata + symbol_index) = std::byte{ 2 };
  check_error(errc::invalid_frame, [&] { static_cast<void>(dacoexec::huffman::decompress(huffman)); });
  for (auto coder : { dacoexec::ans::entropy_coder::rans, dacoexec::ans::entropy_coder::tans }) {
    auto ans = dacoexec::ans::compress(input, { .coder_ = coder });
    constexpr auto ans_metadata = dacoexec::detail::ans::archive_header_size + dacoexec::detail::ans::frame_header_size;
    ans.at(ans_metadata) = std::byte{ 0 };// Invalid model precision.
    check_error(errc::invalid_frame, [&] { static_cast<void>(dacoexec::ans::decompress(ans)); });
  }
  auto block = dacoexec::detail::huffman::compress_block({ 0, input });
  block.lengths_.at(symbol_index) = 2;
  check_error(errc::invalid_payload,
    [&] { static_cast<void>(dacoexec::detail::huffman::decompress_block(std::move(block), input.size())); });
}

TEST_CASE("Integer primitives and public views expose structured errors", "[error]")
{
  namespace integers = dacoexec::detail::integer_coding;
  check_error(errc::invalid_argument, [] { static_cast<void>(integers::gamma_bit_length(0)); });
  check_error(errc::invalid_argument, [] { static_cast<void>(integers::golomb_bit_length(1, 0)); });
  check_error(errc::numeric_overflow,
    [] { static_cast<void>(integers::golomb_bit_length(std::numeric_limits<std::uint64_t>::max(), 1)); });
  std::array<bool, 1> const bits{ false };
  auto truncated = bits | dacoexec::views::decode::gamma(1);
  check_error(errc::truncated_input, [&] { static_cast<void>(truncated.begin()); });
  check_error(errc::invalid_argument, [] { static_cast<void>(dacoexec::views::decode::golomb(0, 1)); });
}

TEST_CASE("tANS payload bit count overflow is independent of output limits", "[error][tans]")
{
  namespace tans = dacoexec::detail::ans::tans;
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  static_assert(tans::checked_payload_bit_count(maximum, 0) == maximum);
  static_assert(tans::checked_payload_bit_count(maximum - 1, 1) == maximum);
  check_error(errc::numeric_overflow, [&] { static_cast<void>(tans::checked_payload_bit_count(maximum, 1)); });
  check_error(errc::numeric_overflow, [&] { static_cast<void>(tans::checked_payload_bit_count(maximum - 1, 2)); });
}

TEST_CASE("Fuzz rejection policies admit only expected input failures", "[error][fuzz]")
{
  using namespace dacoexec::fuzz;
  for (int value = 1; value <= static_cast<int>(errc::codeword_limit_exceeded); ++value) {
    auto const code = static_cast<errc>(value);
    bool const payload = code == errc::invalid_payload;
    bool const limit = code == errc::output_limit_exceeded;
    bool const framing = code == errc::invalid_archive || code == errc::truncated_archive || code == errc::invalid_frame
                         || code == errc::checksum_mismatch;
    CHECK(expected_archive_error(code) == (framing || payload || limit));
    CHECK(expected_huffman_block_error(code) == (payload || limit));
    CHECK(expected_ans_block_error(code) == payload);
  }
  std::error_code const foreign(6, std::generic_category());
  CHECK_FALSE(expected_archive_error(foreign));
  CHECK_FALSE(expected_huffman_block_error(foreign));
  CHECK_FALSE(expected_ans_block_error(foreign));
}
