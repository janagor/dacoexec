#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <algorithm>
#include <iterator>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST_CASE("Arithmetic parallel compression matches sequential archive bytes", "[arithmetic][execution]")
{
  namespace arithmetic = dacoexec::arithmetic;
  using bytes = std::vector<std::byte>;
  for (auto workers : { 1U, 4U }) {
    exec::static_thread_pool pool{ workers };
    for (auto capacity : { 1U, 2U, 4U }) {
      for (auto size : { 0U, 1U, 4096U, 8199U, 20480U, 20487U }) {
        for (bool randomized : { false, true }) {
          CAPTURE(workers, capacity, size, randomized);
          bytes input(size, std::byte{ 'A' });
          for (std::size_t i = 4096; i < std::min(input.size(), std::size_t{ 8192 }); ++i) {
            input[i] = static_cast<std::byte>(i % 256);
          }
          if (randomized) {
            std::mt19937 random{ 42 };
            // A seeded multi-symbol source exercises model construction on every frame.
            for (auto &value : input) { value = static_cast<std::byte>(random() % 17); }
          }
          bytes archive;
          auto sender = arithmetic::compress_parallel_on(pool.get_scheduler(),
            bytes(input),
            std::back_inserter(archive),
            { .block_size_ = 4096, .max_in_flight_ = capacity });
          CHECK(archive.empty());
          auto result = stdexec::sync_wait(std::move(sender));
          REQUIRE(result.has_value());
          auto const &stats = std::get<0>(*result);
          CHECK(stats.input_bytes_ == input.size());
          CHECK(stats.output_bytes_ == archive.size());
          CHECK(stats.frames_ == (size + 4095) / 4096);
          CHECK(archive == arithmetic::compress(input, { .block_size_ = 4096 }));
          CHECK(arithmetic::decompress(archive) == input);
        }
      }
    }
  }
  bytes output;
  auto scheduler = stdexec::inline_scheduler{};
  REQUIRE_THROWS_MATCHES(
    stdexec::sync_wait(arithmetic::compress_parallel_on(
      scheduler, bytes{}, std::back_inserter(output), { .coder_ = static_cast<arithmetic::entropy_coder>(255) })),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::unsupported_coder));
  CHECK(output.empty());
  REQUIRE_THROWS_AS(stdexec::sync_wait(arithmetic::compress_parallel_on(
                      scheduler, bytes{}, std::back_inserter(output), { .max_in_flight_ = 0 })),
    dacoexec::exception);
  std::ostringstream stream;
  REQUIRE(stdexec::sync_wait(arithmetic::compress_parallel_on(scheduler, bytes{}, stream)).has_value());
  CHECK(stream.str().size() == arithmetic::compress(bytes{}).size());
}

TEST_CASE("Arithmetic parallel compression preserves committed frames on sink failure", "[arithmetic][execution]")
{
  namespace arithmetic = dacoexec::arithmetic;
  using bytes = std::vector<std::byte>;
  constexpr std::size_t block_size = 4096;
  bytes input(block_size * 7, std::byte{ 'A' });
  // Give successive frames different contents, including a RAW frame.
  for (std::size_t i = block_size; i < block_size * 2; ++i) { input[i] = static_cast<std::byte>(i % 256); }
  std::fill(input.begin() + block_size * 2, input.end(), std::byte{ 'B' });

  struct failing_sink
  {
    std::size_t limit_;
    bytes bytes_;
    unsigned failures_ = 0;

    void write(std::span<std::byte const> data)
    {
      if (data.size() > limit_ - bytes_.size()) {
        ++failures_;
        throw std::runtime_error("compression sink failed");
      }
      bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
  };

  for (auto workers : { 1U, 4U }) {
    exec::static_thread_pool pool{ workers };
    for (auto capacity : { 1U, 2U, 4U }) {
      for (auto committed_frames : { 1U, 3U }) {
        CAPTURE(workers, capacity, committed_frames);
        auto const prefix_input = std::span(input).first(block_size * committed_frames);
        auto prefix = arithmetic::compress(prefix_input, { .block_size_ = block_size });
        // Strip END to obtain precisely the bytes of the successfully committed frames.
        prefix.resize(prefix.size() - dacoexec::detail::arithmetic::frame_header_size);
        failing_sink output{ prefix.size(), {}, 0 };
        REQUIRE_THROWS_WITH(
          stdexec::sync_wait(arithmetic::compress_parallel_on(
            pool.get_scheduler(), input, output, { .block_size_ = block_size, .max_in_flight_ = capacity })),
          "compression sink failed");
        CHECK(output.failures_ == 1);
        CHECK(output.bytes_ == prefix);
        // Earlier frames remain readable, but END must not be appended after the failure.
        bytes restored;
        REQUIRE_THROWS_MATCHES(arithmetic::decompress_stream(output.bytes_, std::back_inserter(restored)),
          dacoexec::exception,
          dacoexec::test::error_is(dacoexec::errc::truncated_archive));
        CHECK(std::ranges::equal(restored, prefix_input));
      }
    }
  }
}

TEST_CASE("Adaptive and binary parallel streams reproduce sequential bytes and decode mixed frames",
  "[arithmetic][execution]")
{
  namespace arithmetic = dacoexec::arithmetic;
  using bytes = std::vector<std::byte>;
  bytes input(100000, std::byte{ 'A' });
  for (std::size_t i = 4096; i < 8192; ++i) { input[i] = static_cast<std::byte>(i % 256); }
  for (auto coder : { arithmetic::entropy_coder::adaptive_range,
         arithmetic::entropy_coder::binary,
         arithmetic::entropy_coder::context_binary }) {
    auto const expected = arithmetic::compress(input, { .block_size_ = 4096, .coder_ = coder });
    for (auto workers : { 1U, 4U }) {
      exec::static_thread_pool pool{ workers };
      for (auto capacity : { 1U, 3U }) {
        bytes archive;
        REQUIRE(stdexec::sync_wait(arithmetic::compress_parallel_on(pool.get_scheduler(),
                                     input,
                                     std::back_inserter(archive),
                                     { .block_size_ = 4096, .max_in_flight_ = capacity, .coder_ = coder }))
            .has_value());
        CHECK(archive == expected);
        bytes output;
        REQUIRE(stdexec::sync_wait(
          arithmetic::decompress_parallel_on(
            pool.get_scheduler(), archive, std::back_inserter(output), { .max_in_flight_ = capacity }))
            .has_value());
        CHECK(output == input);
      }
    }
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
