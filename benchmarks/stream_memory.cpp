#include "fixtures.hpp"

#include <dacoexec/huffman/execution.hpp>
#include <dacoexec/huffman/format.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>

namespace {
struct counting_sink
{
  std::uint64_t size_ = 0;
  auto write(std::span<std::byte const> bytes) -> void { size_ += bytes.size(); }
};
}// namespace

auto main(int argc, char **argv) -> int
{
  try {
    constexpr int expected_arguments = 6;
    if (argc != expected_arguments) { return 1; }
    auto const arguments = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    constexpr std::size_t size_argument = 0;
    constexpr std::size_t block_argument = 1;
    constexpr std::size_t capacity_argument = 2;
    constexpr std::size_t workers_argument = 3;
    constexpr std::size_t dataset_argument = 4;
    auto const size = std::stoull(arguments.subspan(size_argument).front());
    auto const block_size = static_cast<std::size_t>(std::stoull(arguments.subspan(block_argument).front()));
    auto const in_flight = static_cast<std::size_t>(std::stoull(arguments.subspan(capacity_argument).front()));
    auto const workers = static_cast<std::uint32_t>(std::stoul(arguments.subspan(workers_argument).front()));
    auto const dataset = std::string(arguments.subspan(dataset_argument).front());
    if (dataset != "zero" && dataset != "uniform") { throw std::invalid_argument("Expected zero or uniform dataset"); }
    auto const uniform = dataset == "uniform";
    auto source =
      std::views::iota(std::uint64_t{ 0 }, size) | std::views::transform([uniform](std::uint64_t index) -> std::byte {
        return uniform ? fixtures::uniform_byte(index) : std::byte{ 0 };
      });
    counting_sink sink;
    exec::static_thread_pool pool{ workers };
    auto result = stdexec::sync_wait(dacoexec::huffman::encode_parallel_on(
      pool.get_scheduler(), source, sink, { .block_size_ = block_size, .max_in_flight_ = in_flight }));
    if (!result) { throw std::runtime_error("Incomplete memory probe"); }
    auto const &[statistics] = *result;
    if (statistics.input_bytes_ != size) { throw std::runtime_error("Incomplete memory probe"); }
    if (uniform) {
      auto const raw_archive_size = size + (statistics.frames_ * dacoexec::huffman::frame_header_size)
                                    + dacoexec::huffman::archive_header_size + dacoexec::huffman::frame_header_size;
      if (sink.size_ != raw_archive_size) {
        throw std::runtime_error("Uniform memory probe did not use RAW throughout");
      }
    }
    std::cout << sink.size_ << '\n';
    return 0;
  } catch (std::exception const &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    // stdexec assertion failures need not derive from std::exception.
    std::cerr << "Memory probe failed with a non-standard exception\n";
    return 1;
  }
}
