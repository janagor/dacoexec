#include <benchmark/benchmark.h>
#include <cstdlib>
#include <dacoexec/ans.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/views/ans_frames.hpp>
#include <dacoexec/detail/views/frames.hpp>
#include <dacoexec/huffman.hpp>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace {
auto corpus(std::size_t size, unsigned alphabet) -> std::vector<std::byte>
{
  std::mt19937 random(42);
  std::vector<std::byte> input(size);
  for (auto &value : input) { value = static_cast<std::byte>(random() % alphabet); }
  return input;
}

// Parse an archive once, outside the timed loop. Percentages count data frames
// (not bytes); an empty archive has zero for every percentage.
void report_frames(benchmark::State &state, std::span<std::byte const> archive, bool huffman)
{
  std::size_t raw = 0;
  std::size_t rans = 0;
  std::size_t tans = 0;
  std::size_t canonical = 0;
  if (huffman) {
    for (auto const &frame : archive | dacoexec::detail::views::frames()) {
      if (frame.header_.kind_ == dacoexec::detail::huffman::frame_kind::raw) {
        ++raw;
      } else {
        ++canonical;
      }
    }
  } else {
    for (auto const &frame : archive | dacoexec::detail::views::ans_frames()) {
      switch (frame.header_.kind_) {
      case dacoexec::detail::ans::frame_kind::raw:
        ++raw;
        break;
      case dacoexec::detail::ans::frame_kind::rans:
        ++rans;
        break;
      case dacoexec::detail::ans::frame_kind::tans:
        ++tans;
        break;
      case dacoexec::detail::ans::frame_kind::end:
        break;
      }
    }
  }
  auto const total = raw + rans + tans + canonical;
  auto const percent = [total](std::size_t count) {
    return total == 0 ? 0.0 : 100.0 * static_cast<double>(count) / static_cast<double>(total);
  };
  state.counters["frames"] = static_cast<double>(total);
  state.counters["raw_frame_pct"] = percent(raw);
  state.counters["rans_frame_pct"] = percent(rans);
  state.counters["tans_frame_pct"] = percent(tans);
  state.counters["huffman_frame_pct"] = percent(canonical);
}

void archive(benchmark::State &state)
{
  auto input = corpus(static_cast<std::size_t>(state.range(0)), static_cast<unsigned>(state.range(1)));
  auto const coder = state.range(2);
  std::vector<std::byte> encoded;
  for (auto iteration : state) {
    static_cast<void>(iteration);
    auto output =
      coder == 2
        ? dacoexec::huffman::compress(input)
        : dacoexec::ans::compress(
            input, { .coder_ = coder == 0 ? dacoexec::ans::entropy_coder::rans : dacoexec::ans::entropy_coder::tans });
    benchmark::DoNotOptimize(output.data());
    encoded = std::move(output);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
  state.counters["archive_bytes"] = static_cast<double>(encoded.size());
  report_frames(state, encoded, coder == 2);
}

void table_setup(benchmark::State &state)
{
  namespace ans = dacoexec::detail::ans;
  ans::frequency_table counts{};
  auto const table_log = static_cast<unsigned>(state.range(0));
  auto const alphabet = static_cast<unsigned>(state.range(1));
  bool const skewed = state.range(2) != 0;
  for (unsigned symbol = 0; symbol < alphabet; ++symbol) { counts.at(symbol) = skewed && symbol == 0 ? 1000000U : 1U; }
  auto model = ans::make_model(counts, table_log);
  for (auto iteration : state) {
    static_cast<void>(iteration);
    dacoexec::detail::ans::tans::tables table(model);
    benchmark::DoNotOptimize(table);
  }
}

void archive_decode(benchmark::State &state)
{
  auto input = corpus(static_cast<std::size_t>(state.range(0)), 17);
  auto const coder = state.range(1);
  auto encoded =
    coder == 2 ? dacoexec::huffman::compress(input)
               : dacoexec::ans::compress(input,
                   { .coder_ = coder == 0 ? dacoexec::ans::entropy_coder::rans : dacoexec::ans::entropy_coder::tans });
  for (auto iteration : state) {
    static_cast<void>(iteration);
    auto decoded = coder == 2 ? dacoexec::huffman::decompress(encoded) : dacoexec::ans::decompress(encoded);
    benchmark::DoNotOptimize(decoded.data());
  }
  report_frames(state, encoded, coder == 2);
  state.counters["archive_bytes"] = static_cast<double>(encoded.size());
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
}

void frequency_count(benchmark::State &state)
{
  auto input = corpus(static_cast<std::size_t>(state.range(0)), 17);
  for (auto iteration : state) {
    static_cast<void>(iteration);
    auto counts = dacoexec::detail::ans::count_frequencies(input);
    benchmark::DoNotOptimize(counts);
  }
}

void normalization(benchmark::State &state)
{
  auto input = corpus(static_cast<std::size_t>(state.range(0)), 17);
  auto counts = dacoexec::detail::ans::count_frequencies(input);
  for (auto iteration : state) {
    static_cast<void>(iteration);
    auto model = dacoexec::detail::ans::make_model(counts, 10);
    benchmark::DoNotOptimize(model);
  }
}

// These include output allocation and bit packing, but reuse an immutable table.
void symbol_encode(benchmark::State &state)
{
  auto input = corpus(static_cast<std::size_t>(state.range(0)), static_cast<unsigned>(state.range(1)));
  auto model = dacoexec::detail::ans::make_model(dacoexec::detail::ans::count_frequencies(input), 10);
  dacoexec::detail::ans::tans::tables table(model);
  for (auto iteration : state) {
    static_cast<void>(iteration);
    auto payload = dacoexec::detail::ans::tans::encode_block(input, table);
    benchmark::DoNotOptimize(payload.data());
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
}

void symbol_decode(benchmark::State &state)
{
  auto input = corpus(static_cast<std::size_t>(state.range(0)), static_cast<unsigned>(state.range(1)));
  auto model = dacoexec::detail::ans::make_model(dacoexec::detail::ans::count_frequencies(input), 10);
  dacoexec::detail::ans::tans::tables table(model);
  auto payload = dacoexec::detail::ans::tans::encode_block(input, table);
  for (auto iteration : state) {
    static_cast<void>(iteration);
    auto decoded = dacoexec::detail::ans::tans::decode_block(payload, table, input.size());
    benchmark::DoNotOptimize(decoded.data());
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
}

// Optional real-file runs use the complete file, loaded outside the timing loop.
void file_archive(benchmark::State &state, bool decoding)
{
  auto const *path = std::getenv("DACOEXEC_BENCHMARK_INPUT");
  if (path == nullptr) {
    state.SkipWithError("Set DACOEXEC_BENCHMARK_INPUT");
    return;
  }
  std::ifstream source(path, std::ios::binary);
  if (!source) {
    state.SkipWithError("Cannot open benchmark input");
    return;
  }
  std::vector<char> chars{ std::istreambuf_iterator<char>{ source }, std::istreambuf_iterator<char>{} };
  if (source.bad()) {
    state.SkipWithError("Cannot read benchmark input");
    return;
  }
  auto const input = std::as_bytes(std::span{ chars });
  auto const coder = state.range(0);
  auto encode = [&] {
    return coder == 2
             ? dacoexec::huffman::compress(input)
             : dacoexec::ans::compress(input,
                 { .coder_ = coder == 0 ? dacoexec::ans::entropy_coder::rans : dacoexec::ans::entropy_coder::tans });
  };
  std::vector<std::byte> encoded;
  if (decoding) { encoded = encode(); }
  std::size_t archive_size = encoded.size();
  for (auto iteration : state) {
    static_cast<void>(iteration);
    std::vector<std::byte> output;
    if (decoding) {
      output = coder == 2 ? dacoexec::huffman::decompress(encoded) : dacoexec::ans::decompress(encoded);
    } else {
      output = encode();
    }
    benchmark::DoNotOptimize(output.data());
    if (!decoding) {
      archive_size = output.size();
      encoded = std::move(output);
    }
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
  state.counters["archive_bytes"] = static_cast<double>(archive_size);
  report_frames(state, encoded, coder == 2);
}

BENCHMARK(frequency_count)->Arg(4096)->Arg(65536)->Arg(1048576)->Repetitions(5)->ReportAggregatesOnly();
BENCHMARK(normalization)->Arg(4096)->Arg(65536)->Arg(1048576)->Repetitions(5)->ReportAggregatesOnly();
BENCHMARK(symbol_encode)
  ->ArgsProduct({ { 4096, 65536, 1048576 }, { 2, 17, 256 } })
  ->Repetitions(5)
  ->ReportAggregatesOnly();
BENCHMARK(symbol_decode)
  ->ArgsProduct({ { 4096, 65536, 1048576 }, { 2, 17, 256 } })
  ->Repetitions(5)
  ->ReportAggregatesOnly();
BENCHMARK(table_setup)
  ->ArgsProduct({ { 8, 9, 10, 11, 12 }, { 1, 2, 17, 256 }, { 0, 1 } })
  ->Repetitions(5)
  ->ReportAggregatesOnly();
BENCHMARK(archive_decode)
  ->ArgsProduct({ { 4096, 65536, 1048576 }, { 0, 1, 2 } })
  ->Repetitions(5)
  ->ReportAggregatesOnly();
BENCHMARK(archive)
  ->ArgsProduct({ { 4096, 65536, 1048576 }, { 2, 17, 256 }, { 0, 1, 2 } })
  ->Repetitions(5)
  ->ReportAggregatesOnly();
}// namespace

int main(int argc, char **argv)
{
  if (std::getenv("DACOEXEC_BENCHMARK_INPUT") != nullptr) {
    benchmark::RegisterBenchmark("file_archive_encode", [](benchmark::State &state) { file_archive(state, false); })
      ->Arg(0)
      ->Arg(1)
      ->Arg(2)
      ->Repetitions(5)
      ->ReportAggregatesOnly();
    benchmark::RegisterBenchmark("file_archive_decode", [](benchmark::State &state) { file_archive(state, true); })
      ->Arg(0)
      ->Arg(1)
      ->Arg(2)
      ->Repetitions(5)
      ->ReportAggregatesOnly();
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) { return 1; }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
