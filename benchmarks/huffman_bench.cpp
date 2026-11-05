#include "fixtures.hpp"

#include <benchmark/benchmark.h>
#include <dacoexec/execution/ordered_parallel.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/execution.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/stream.hpp>
#include <dacoexec/io/output_sink.hpp>
#include <dacoexec/views/blocks.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace huffman = dacoexec::huffman;
using bytes = std::vector<std::byte>;
constexpr std::int64_t representative_in_flight = 4;
constexpr std::size_t block_parameter = 0;
constexpr std::size_t dataset_parameter = 1;
constexpr std::size_t workers_parameter = 2;
constexpr std::size_t in_flight_parameter = 3;
constexpr std::size_t scheduled_parameter = 4;
struct counting_sink
{
  std::size_t size_ = 0;
  auto write(std::span<std::byte const> input) -> void { size_ += input.size(); }
};
auto metrics(benchmark::State &state, std::size_t input, std::size_t output) -> void
{
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input));
  state.counters["compression_ratio"] = static_cast<double>(output) / static_cast<double>(input);
  state.counters["encoded_bytes"] = static_cast<double>(output);
  state.counters["seed"] = fixtures::seed;
  state.SetLabel(std::string(fixtures::names.at(static_cast<std::size_t>(state.range(dataset_parameter)))));
}
auto fixture(benchmark::State const &state, std::size_t size) -> bytes
{ return fixtures::make(size, static_cast<fixtures::dataset>(state.range(dataset_parameter))); }
auto bm_huffman_compress_block(benchmark::State &state) -> void
{
  auto const input = fixture(state, static_cast<std::size_t>(state.range(block_parameter)));
  auto const reference = huffman::compress_block({ 0, input });
  auto validation = huffman::compress_block({ 0, input });
  if (huffman::decompress_block(std::move(validation), input.size()).bytes_ != input) {
    state.SkipWithError("Block round trip differs");
    return;
  }
  for (auto iteration : state) {
    static_cast<void>(iteration);
    state.PauseTiming();
    dacoexec::byte_block copy{ 0, input };
    state.ResumeTiming();
    auto encoded = huffman::compress_block(std::move(copy));
    benchmark::DoNotOptimize(encoded.payload_.data());
  }
  metrics(state,
    input.size(),
    reference.payload_.size()
      + (reference.encoding_ == huffman::block_encoding::canonical_huffman ? huffman::alphabet_size : 0U));
}
auto bm_huffman_decompress_block(benchmark::State &state) -> void
{
  auto const input = fixture(state, static_cast<std::size_t>(state.range(block_parameter)));
  auto const encoded = huffman::compress_block({ 0, input });
  for (auto iteration : state) {
    static_cast<void>(iteration);
    state.PauseTiming();
    huffman::encoded_block copy;
    copy.encoding_ = encoded.encoding_;
    copy.uncompressed_size_ = encoded.uncompressed_size_;
    copy.payload_ = encoded.payload_;
    copy.lengths_ = encoded.lengths_;
    copy.padding_bits_ = encoded.padding_bits_;
    state.ResumeTiming();
    auto decoded = huffman::decompress_block(std::move(copy), input.size());
    benchmark::DoNotOptimize(decoded.bytes_.data());
  }
  metrics(state,
    input.size(),
    encoded.payload_.size()
      + (encoded.encoding_ == huffman::block_encoding::canonical_huffman ? huffman::alphabet_size : 0U));
}
auto bm_huffman_compress_stream(benchmark::State &state) -> void
{
  auto const input = fixture(state, fixtures::stream_size);
  auto const block_size = static_cast<std::size_t>(state.range(block_parameter));
  counting_sink reference;
  static_cast<void>(huffman::compress_stream(input, reference, { block_size }));
  for (auto iteration : state) {
    static_cast<void>(iteration);
    counting_sink sink;
    auto statistics = huffman::compress_stream(input, sink, { block_size });
    benchmark::DoNotOptimize(statistics.output_bytes_);
  }
  metrics(state, input.size(), reference.size_);
}
auto bm_huffman_decompress_stream(benchmark::State &state) -> void
{
  auto const input = fixture(state, fixtures::stream_size);
  bytes archive;
  dacoexec::io::iterator_sink destination{ std::back_inserter(archive) };
  static_cast<void>(
    huffman::compress_stream(input, destination, { static_cast<std::size_t>(state.range(block_parameter)) }));
  for (auto iteration : state) {
    static_cast<void>(iteration);
    counting_sink sink;
    auto statistics = huffman::decompress_stream(archive, sink);
    benchmark::DoNotOptimize(statistics.output_bytes_);
  }
  metrics(state, input.size(), archive.size());
}
auto bm_huffman_compress_parallel(benchmark::State &state) -> void
{
  auto const input = fixture(state, fixtures::stream_size);
  auto const block_size = static_cast<std::size_t>(state.range(block_parameter));
  exec::static_thread_pool pool{ static_cast<std::uint32_t>(state.range(workers_parameter)) };
  std::size_t reference_size = 0;
  {
    // Preflight is outside timing and releases all collected buffers before measurement.
    bytes reference;
    dacoexec::io::iterator_sink sequential{ std::back_inserter(reference) };
    static_cast<void>(huffman::compress_stream(input, sequential, { .block_size_ = block_size }));
    bytes parallel;
    dacoexec::io::iterator_sink output{ std::back_inserter(parallel) };
    auto result = stdexec::sync_wait(huffman::encode_parallel_on(pool.get_scheduler(),
      input,
      output,
      { .block_size_ = block_size, .max_in_flight_ = static_cast<std::size_t>(state.range(in_flight_parameter)) }));
    if (!result || parallel != reference) {
      state.SkipWithError("Parallel archive differs from sequential archive");
      return;
    }
    bytes decoded;
    dacoexec::io::iterator_sink plain{ std::back_inserter(decoded) };
    static_cast<void>(huffman::decompress_stream(parallel, plain));
    if (decoded != input) {
      state.SkipWithError("Parallel archive round trip differs");
      return;
    }
    reference_size = reference.size();
  }
  for (auto iteration : state) {
    static_cast<void>(iteration);
    counting_sink sink;
    auto result = stdexec::sync_wait(huffman::encode_parallel_on(pool.get_scheduler(),
      input,
      sink,
      { .block_size_ = block_size, .max_in_flight_ = static_cast<std::size_t>(state.range(in_flight_parameter)) }));
    if (!result || sink.size_ != reference_size) {
      state.SkipWithError("Parallel output size mismatch");
      break;
    }
    benchmark::DoNotOptimize(sink.size_);
  }
  metrics(state, input.size(), reference_size);
}

// Same frequency-counting workload directly and through the public ordered sender.
struct frequency_source
{
  using input_type = std::span<std::byte const>;
  using result_type = huffman::frequency_table;
  std::span<std::byte const> input_;
  std::size_t block_size_;
  std::uint64_t committed_ = 0;
  auto initialize() -> void {}
  auto next() -> std::optional<input_type>
  {
    if (input_.empty()) { return std::nullopt; }
    auto const block = input_.first(block_size_);
    input_ = input_.subspan(block_size_);
    return block;
  }
  auto commit(result_type const &frequencies) -> void
  {
    for (auto count : frequencies) { committed_ += count; }
  }
  [[nodiscard]] auto finish() const -> std::uint64_t { return committed_; }
};
auto bm_huffman_scheduling_overhead(benchmark::State &state) -> void
{
  auto const input = fixture(state, fixtures::stream_size);
  auto const block_size = static_cast<std::size_t>(state.range(block_parameter));
  exec::static_thread_pool pool{ static_cast<std::uint32_t>(state.range(workers_parameter)) };
  auto process = [](std::span<std::byte const> block) -> huffman::frequency_table {
    return huffman::count_frequencies(block);
  };
  for (auto iteration : state) {
    static_cast<void>(iteration);
    frequency_source source{ .input_ = input, .block_size_ = block_size };
    if (state.range(scheduled_parameter) != 0) {
      auto result = stdexec::sync_wait(dacoexec::execution::ordered_parallel_on(
        pool.get_scheduler(), source, process, static_cast<std::size_t>(state.range(in_flight_parameter))));
      benchmark::DoNotOptimize(result);
    } else {
      while (auto block = source.next()) { source.commit(process(*block)); }
      benchmark::DoNotOptimize(source.committed_);
    }
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
}
auto block_arguments(benchmark::Benchmark *benchmark) -> void
{
  benchmark->ArgNames({ "block_bytes", "dataset" });
  for (auto size : fixtures::block_sizes) {
    for (auto kind : fixtures::datasets) {
      benchmark->Args({ static_cast<std::int64_t>(size), static_cast<std::int64_t>(kind) });
    }
  }
}
auto execution_arguments(benchmark::Benchmark *benchmark, bool scheduling) -> void
{
  auto names = std::vector<std::string>{ "block_bytes", "dataset", "workers", "in_flight" };
  if (scheduling) { names.emplace_back("scheduled"); }
  benchmark->ArgNames(names);
  for (auto size : fixtures::block_sizes) {
    for (auto workers : fixtures::concurrency) {
      for (auto capacity : fixtures::concurrency) {
#ifndef DACOEXEC_BENCHMARK_FULL_MATRIX
        if (capacity != representative_in_flight && workers != representative_in_flight) { continue; }
#endif
        std::vector<std::int64_t> args{
          static_cast<std::int64_t>(size), static_cast<std::int64_t>(fixtures::dataset::text), workers, capacity
        };
        if (scheduling) {
          args.push_back(0);
          benchmark->Args(args);
          args.back() = 1;
        }
        benchmark->Args(args);
      }
    }
  }
}
auto parallel_arguments(benchmark::Benchmark *benchmark) -> void { execution_arguments(benchmark, false); }
auto scheduling_arguments(benchmark::Benchmark *benchmark) -> void { execution_arguments(benchmark, true); }
BENCHMARK(bm_huffman_compress_block)->Apply(block_arguments);
BENCHMARK(bm_huffman_decompress_block)->Apply(block_arguments);
BENCHMARK(bm_huffman_compress_stream)->Apply(block_arguments)->UseRealTime();
BENCHMARK(bm_huffman_decompress_stream)->Apply(block_arguments)->UseRealTime();
BENCHMARK(bm_huffman_compress_parallel)->Apply(parallel_arguments)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(bm_huffman_scheduling_overhead)->Apply(scheduling_arguments)->UseRealTime()->MeasureProcessCPUTime();
}// namespace
BENCHMARK_MAIN();
