#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using bytes = std::vector<std::byte>;
constexpr std::size_t frame_size = 4096;

struct observed_sink
{
  bytes bytes_;

  auto write(std::span<std::byte const> data) -> void { bytes_.insert(bytes_.end(), data.begin(), data.end()); }
};

template<bool use_ans> struct codec
{
  static auto compress(bytes const &input, bool tans = false) -> bytes
  {
    if constexpr (use_ans) {
      return dacoexec::ans::compress(input,
        { .block_size_ = frame_size,
          .coder_ = tans ? dacoexec::ans::entropy_coder::tans : dacoexec::ans::entropy_coder::rans });
    } else {
      return dacoexec::huffman::compress(input, { .block_size_ = frame_size });
    }
  }

  template<class Scheduler, class R, class Destination>
  static auto decode(Scheduler scheduler,
    R &&source,
    Destination &&destination,
    std::size_t capacity = 2,
    std::uint64_t output_limit = std::numeric_limits<std::uint64_t>::max(),
    std::size_t limit = frame_size)
  {
    if constexpr (use_ans) {
      return dacoexec::ans::decompress_parallel_on(std::move(scheduler),
        std::forward<R>(source),
        std::forward<Destination>(destination),
        { .max_frame_size_ = limit, .max_output_bytes_ = output_limit, .max_in_flight_ = capacity });
    } else {
      return dacoexec::huffman::decompress_parallel_on(std::move(scheduler),
        std::forward<R>(source),
        std::forward<Destination>(destination),
        { .max_frame_size_ = limit, .max_output_bytes_ = output_limit, .max_in_flight_ = capacity });
    }
  }
};

template<bool use_ans> auto check_round_trips() -> void
{
  for (auto workers : { 1U, 4U }) {
    exec::static_thread_pool pool{ workers };
    for (auto capacity : { 1U, 2U, 4U }) {
      for (auto size : { std::size_t{ 0 }, frame_size, (frame_size * 5) + 7 }) {
        bytes input(size, std::byte{ 'A' });
        // Include compressed, RAW, and short final frames in the same archive.
        for (std::size_t i = frame_size; i < std::min(size, frame_size * 2); ++i) {
          input[i] = static_cast<std::byte>(i % 256);
        }
        for (bool tans : { false, true }) {
          auto archive = codec<use_ans>::compress(input, tans);
          bytes output;
          auto result = stdexec::sync_wait(
            codec<use_ans>::decode(pool.get_scheduler(), archive, std::back_inserter(output), capacity, size));
          REQUIRE(result.has_value());
          auto const &stats = std::get<0>(*result);
          CHECK(stats.input_bytes_ == archive.size());
          CHECK(stats.output_bytes_ == input.size());
          CHECK(stats.frames_ == (size + frame_size - 1) / frame_size);
          CHECK(output == input);
        }
      }
    }
  }
}

template<bool use_ans> auto check_failures() -> void
{
  exec::static_thread_pool pool{ 2 };
  bytes const input(frame_size * 5, std::byte{ 'A' });
  auto const archive = codec<use_ans>::compress(input);
  observed_sink output;
  auto expect_error = [&](auto source,
                        dacoexec::errc error,
                        std::size_t capacity = 2,
                        std::uint64_t output_limit = std::numeric_limits<std::uint64_t>::max(),
                        std::size_t limit = frame_size) -> void {
    output.bytes_.clear();
    REQUIRE_THROWS_MATCHES(stdexec::sync_wait(codec<use_ans>::decode(
                             pool.get_scheduler(), std::move(source), output, capacity, output_limit, limit)),
      dacoexec::exception,
      dacoexec::test::error_is(error));
  };
  expect_error(archive, dacoexec::errc::invalid_argument, 0);
  CHECK(output.bytes_.empty());
  expect_error(archive, dacoexec::errc::invalid_argument, 2, std::numeric_limits<std::uint64_t>::max(), 0);
  CHECK(output.bytes_.empty());
  expect_error(archive,
    dacoexec::errc::invalid_argument,
    2,
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::size_t>::max());
  CHECK(output.bytes_.empty());
  expect_error(archive, dacoexec::errc::output_limit_exceeded, 2, 0);
  CHECK(output.bytes_.empty());
  expect_error(archive, dacoexec::errc::output_limit_exceeded, 2, frame_size);
  CHECK(output.bytes_.empty());// the second frame fails reservation before dispatch
  expect_error(archive, dacoexec::errc::output_limit_exceeded, 2, frame_size * 3);
  CHECK(output.bytes_ == bytes(frame_size * 2, std::byte{ 'A' }));

  expect_error(
    archive, dacoexec::errc::output_limit_exceeded, 2, std::numeric_limits<std::uint64_t>::max(), frame_size - 1);
  CHECK(output.bytes_.empty());
  auto invalid_magic = archive;
  invalid_magic.front() = std::byte{};
  expect_error(invalid_magic, dacoexec::errc::invalid_archive);
  CHECK(output.bytes_.empty());

  // Corrupt the CRC of the third frame without affecting parsing.
  auto damaged = archive;
  std::size_t const header_size =
    use_ans ? dacoexec::detail::ans::archive_header_size : dacoexec::detail::huffman::archive_header_size;
  std::size_t const frame_header =
    use_ans ? dacoexec::detail::ans::frame_header_size : dacoexec::detail::huffman::frame_header_size;
  // Identical input blocks produce identical serialized frame lengths.
  std::size_t const frame_bytes = (archive.size() - header_size - frame_header) / 5;
  std::size_t const crc_offset = dacoexec::detail::codec::crc_field_offset;
  damaged.at(header_size + (frame_bytes * 2) + crc_offset) ^= std::byte{ 1 };
  expect_error(damaged, dacoexec::errc::checksum_mismatch);
  CHECK(output.bytes_ == bytes(frame_size * 2, std::byte{ 'A' }));

  damaged = archive;
  damaged.at(header_size + frame_bytes + crc_offset) ^= std::byte{ 1 };
  expect_error(damaged, dacoexec::errc::checksum_mismatch);
  CHECK(output.bytes_.empty());

  auto truncated = archive;
  truncated.resize(truncated.size() - frame_header);
  expect_error(truncated, dacoexec::errc::truncated_archive);
  CHECK(output.bytes_ == bytes(frame_size * 4, std::byte{ 'A' }));
  auto trailing = archive;
  trailing.push_back(std::byte{});
  expect_error(trailing, dacoexec::errc::invalid_archive);
  CHECK(output.bytes_ == bytes(frame_size * 4, std::byte{ 'A' }));

  struct failing_sink
  {
    auto write(std::span<std::byte const>) -> void { throw std::runtime_error("decode sink failed"); }
  } failing;

  REQUIRE_THROWS_WITH(
    stdexec::sync_wait(codec<use_ans>::decode(pool.get_scheduler(), archive, failing)), "decode sink failed");
}

template<bool use_ans> auto check_sources_and_destinations() -> void
{
  bytes const input((frame_size * 3) + 1, std::byte{ 'Z' });
  auto const archive = codec<use_ans>::compress(input);
  std::size_t reads = 0;
  auto source = archive | std::views::transform([&](std::byte value) -> std::byte {
    ++reads;
    return value;
  });
  observed_sink output;
  auto sender = codec<use_ans>::decode(stdexec::inline_scheduler{}, source, output);
  CHECK(reads == 0);
  CHECK(output.bytes_.empty());
  REQUIRE(stdexec::sync_wait(std::move(sender)).has_value());
  CHECK(reads == archive.size());
  CHECK(output.bytes_ == input);

  std::ostringstream stream;
  REQUIRE(stdexec::sync_wait(codec<use_ans>::decode(stdexec::inline_scheduler{}, bytes(archive), stream)).has_value());
  CHECK(stream.str() == std::string(input.size(), 'Z'));

  auto encoded = std::string(reinterpret_cast<char const *>(archive.data()), archive.size());
  std::istringstream single_pass(encoded);
  bytes restored;
  REQUIRE(stdexec::sync_wait(codec<use_ans>::decode(stdexec::inline_scheduler{},
                               std::ranges::istream_view<unsigned char>(single_pass >> std::noskipws),
                               std::back_inserter(restored)))
      .has_value());
  CHECK(restored == input);
}
}// namespace

TEST_CASE("Parallel decompression round trips all frame kinds and reports statistics", "[execution][decode]")
{
  check_round_trips<false>();
  check_round_trips<true>();
}

TEST_CASE("Parallel decompression enforces limits, framing, checksums and sink errors", "[execution][decode]")
{
  check_failures<false>();
  check_failures<true>();
}

TEST_CASE("Parallel decompression is lazy and supports owned and single-pass sources", "[execution][decode]")
{
  check_sources_and_destinations<false>();
  check_sources_and_destinations<true>();
}
