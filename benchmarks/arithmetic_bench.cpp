#include "fixtures.hpp"
#include <benchmark/benchmark.h>
#include <dacoexec/ans.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/detail/arithmetic/fenwick_adaptive_model.hpp>
#include <dacoexec/detail/arithmetic/static_range_codec.hpp>
#include <dacoexec/huffman.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
namespace model = dacoexec::detail::model;
enum class stage { histogram, normalization, encode, decode, serialize };
enum class codec { static_range, huffman, rans, tans, adaptive_range, binary };
using bytes = std::vector<std::byte>;

auto compress(bytes const &input, codec coder, std::size_t block_size) -> bytes
{
  switch (coder) {
  case codec::static_range:
    return dacoexec::arithmetic::compress(input, { .block_size_ = block_size });
  case codec::binary:
    return dacoexec::arithmetic::compress(
      input, { .block_size_ = block_size, .coder_ = dacoexec::arithmetic::entropy_coder::binary });
  case codec::adaptive_range:
    return dacoexec::arithmetic::compress(
      input, { .block_size_ = block_size, .coder_ = dacoexec::arithmetic::entropy_coder::adaptive_range });
  case codec::huffman:
    return dacoexec::huffman::compress(input, { .block_size_ = block_size });
  case codec::rans:
  case codec::tans:
    return dacoexec::ans::compress(input,
      { .block_size_ = block_size,
        .coder_ = coder == codec::rans ? dacoexec::ans::entropy_coder::rans : dacoexec::ans::entropy_coder::tans });
  }
  std::terminate();
}

auto decompress(bytes const &archive, codec coder) -> bytes
{
  switch (coder) {
  case codec::static_range:
  case codec::adaptive_range:
  case codec::binary:
    return dacoexec::arithmetic::decompress(archive);
  case codec::huffman:
    return dacoexec::huffman::decompress(archive);
  case codec::rans:
  case codec::tans:
    return dacoexec::ans::decompress(archive);
  }
  std::terminate();
}

void throughput(benchmark::State &state, std::size_t size)
{ state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size)); }

void micro(benchmark::State &state, stage operation)
{
  auto const input =
    fixtures::make(static_cast<std::size_t>(state.range(0)), static_cast<fixtures::dataset>(state.range(1)));
  auto const counts = model::count_frequencies(input);
  auto const probabilities = model::make_model(counts);
  auto const payload = arithmetic::encode_block(input, probabilities);
  auto const metadata = arithmetic::serialize_model(probabilities);
  if (arithmetic::decode_block(payload, probabilities, input.size(), input.size()) != input) {
    state.SkipWithError("Range block round trip failed");
    return;
  }
  for (auto unused : state) {
    switch (operation) {
    case stage::histogram: {
      auto output = model::count_frequencies(input);
      benchmark::DoNotOptimize(output);
      break;
    }
    case stage::normalization: {
      auto output = model::make_model(counts);
      benchmark::DoNotOptimize(output);
      break;
    }
    case stage::encode: {
      auto output = arithmetic::encode_block(input, probabilities);
      benchmark::DoNotOptimize(output);
      break;
    }
    case stage::decode: {
      auto output = arithmetic::decode_block(payload, probabilities, input.size(), input.size());
      benchmark::DoNotOptimize(output);
      break;
    }
    case stage::serialize: {
      auto output = arithmetic::serialize_model(probabilities);
      benchmark::DoNotOptimize(output);
      break;
    }
    }
  }
  if (operation == stage::normalization || operation == stage::serialize) {
    state.SetItemsProcessed(state.iterations());
  } else {
    throughput(state, input.size());
  }
  state.SetLabel(std::string{ fixtures::names.at(static_cast<std::size_t>(state.range(1))) });
  state.counters["model_bytes"] = static_cast<double>(metadata.size());
  state.counters["payload_bytes"] = static_cast<double>(payload.size());
}

template<class Model> void adaptive_micro(benchmark::State &state, bool decoding)
{
  auto const input =
    fixtures::make(static_cast<std::size_t>(state.range(0)), static_cast<fixtures::dataset>(state.range(1)));
  auto const payload = arithmetic::encode_adaptive_block<Model>(input);
  if (payload != arithmetic::encode_adaptive_block(input)
      || arithmetic::decode_adaptive_block<Model>(payload, input.size(), input.size()) != input) {
    state.SkipWithError("Adaptive range model compatibility or round trip failed");
    return;
  }
  for (auto unused : state) {
    auto output = decoding ? arithmetic::decode_adaptive_block<Model>(payload, input.size(), input.size())
                           : arithmetic::encode_adaptive_block<Model>(input);
    benchmark::DoNotOptimize(output);
  }
  throughput(state, input.size());
  state.SetLabel(std::string{ fixtures::names.at(static_cast<std::size_t>(state.range(1))) });
  state.counters["model_bytes"] = 0;
  state.counters["payload_bytes"] = static_cast<double>(payload.size());
}

void run_archive(benchmark::State &state, bytes const &input, codec coder, bool decoding, std::size_t block_size)
{
  auto const encoded = compress(input, coder, block_size);
  if (decompress(encoded, coder) != input || compress(input, coder, block_size) != encoded) {
    state.SkipWithError("Archive round trip or determinism check failed");
    return;
  }
  for (auto unused : state) {
    auto output = decoding ? decompress(encoded, coder) : compress(input, coder, block_size);
    benchmark::DoNotOptimize(output);
  }
  throughput(state, input.size());
  state.counters["input_bytes"] = static_cast<double>(input.size());
  state.counters["block_bytes"] = static_cast<double>(block_size);
  state.counters["frames"] = static_cast<double>((input.size() + block_size - 1) / block_size);
  state.counters["archive_bytes"] = static_cast<double>(encoded.size());
  state.counters["ratio"] = static_cast<double>(encoded.size()) / static_cast<double>(input.size());
}

void archive(benchmark::State &state, codec coder, bool decoding, bool multi)
{
  auto const size = static_cast<std::size_t>(state.range(0));
  auto const dataset = static_cast<std::size_t>(state.range(1));
  bytes input;
  if (dataset == fixtures::datasets.size()) {
    // Alternate distributions at frame boundaries to exercise RAW fallback,
    // new models, and different serialized model sizes in one archive.
    for (std::size_t offset = 0; offset < size; offset += fixtures::block_sizes.front()) {
      auto block = fixtures::make(std::min(fixtures::block_sizes.front(), size - offset),
        fixtures::datasets.at((offset / fixtures::block_sizes.front()) % 4));
      input.insert(input.end(), block.begin(), block.end());
    }
    state.SetLabel("mixed");
  } else {
    input = fixtures::make(size, fixtures::datasets.at(dataset));
    state.SetLabel(std::string{ fixtures::names.at(dataset) });
  }
  run_archive(state, input, coder, decoding, multi ? fixtures::block_sizes.front() : size);
}

void changing_archive(benchmark::State &state, codec coder, bool decoding)
{
  bytes input;
  for (unsigned symbol = 0; symbol < 8; ++symbol) {
    input.insert(input.end(), fixtures::block_sizes.front(), static_cast<std::byte>(symbol));
  }
  state.SetLabel("changing_distribution");
  run_archive(state, input, coder, decoding, input.size());
}

void real_archive(benchmark::State &state, codec coder, bool decoding, bool multi, std::filesystem::path const &path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    state.SkipWithError("Cannot open corpus file");
    return;
  }
  bytes input;
  for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
    input.push_back(static_cast<std::byte>(static_cast<unsigned char>(*it)));
  }
  if (file.bad() || input.empty()) {
    state.SkipWithError("Corpus file must be readable and nonempty");
    return;
  }
  state.SetLabel(path.filename().string());
  run_archive(state, input, coder, decoding, multi ? fixtures::block_sizes.front() : input.size());
}

void matrix(benchmark::Benchmark *benchmark)
{
  for (auto size : { fixtures::block_sizes.front(), fixtures::mebibyte }) {
    for (auto dataset : fixtures::datasets) {
      benchmark->Args({ static_cast<std::int64_t>(size), static_cast<std::int64_t>(dataset) });
    }
  }
  benchmark->UseRealTime();
}

void archive_matrix(benchmark::Benchmark *benchmark)
{
  matrix(benchmark);
  benchmark->Args(
    { static_cast<std::int64_t>(fixtures::mebibyte), static_cast<std::int64_t>(fixtures::datasets.size()) });
}

void multi_matrix(benchmark::Benchmark *benchmark)
{
  for (std::size_t dataset = 0; dataset <= fixtures::datasets.size(); ++dataset) {
    benchmark->Args({ static_cast<std::int64_t>(fixtures::mebibyte), static_cast<std::int64_t>(dataset) });
  }
  benchmark->UseRealTime();
}

// Binary changes both the alphabet (bits) and the engine (E1/E2/E3);
// range variants use byte symbols and the shared byte-oriented range engine.
BENCHMARK_CAPTURE(changing_archive, binary_encode, codec::binary, false)->UseRealTime();
BENCHMARK_CAPTURE(changing_archive, binary_decode, codec::binary, true)->UseRealTime();
BENCHMARK_CAPTURE(archive, binary_encode, codec::binary, false, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, binary_decode, codec::binary, true, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, binary_multi_encode, codec::binary, false, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, binary_multi_decode, codec::binary, true, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(changing_archive, static_range_encode, codec::static_range, false)->UseRealTime();
BENCHMARK_CAPTURE(changing_archive, static_range_decode, codec::static_range, true)->UseRealTime();
BENCHMARK_CAPTURE(changing_archive, adaptive_range_encode, codec::adaptive_range, false)->UseRealTime();
BENCHMARK_CAPTURE(changing_archive, adaptive_range_decode, codec::adaptive_range, true)->UseRealTime();
BENCHMARK_CAPTURE(adaptive_micro<arithmetic::adaptive_model>, range_encode, false)->Apply(matrix);
BENCHMARK_CAPTURE(adaptive_micro<arithmetic::adaptive_model>, range_decode, true)->Apply(matrix);
BENCHMARK_CAPTURE(adaptive_micro<arithmetic::fenwick_adaptive_model>, fenwick_range_encode, false)->Apply(matrix);
BENCHMARK_CAPTURE(adaptive_micro<arithmetic::fenwick_adaptive_model>, fenwick_range_decode, true)->Apply(matrix);
BENCHMARK_CAPTURE(micro, histogram, stage::histogram)->Apply(matrix);
BENCHMARK_CAPTURE(micro, normalization, stage::normalization)->Apply(matrix);
BENCHMARK_CAPTURE(micro, range_encode, stage::encode)->Apply(matrix);
BENCHMARK_CAPTURE(micro, range_decode, stage::decode)->Apply(matrix);
BENCHMARK_CAPTURE(micro, model_serialization, stage::serialize)->Apply(matrix);
BENCHMARK_CAPTURE(archive, static_range_encode, codec::static_range, false, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, static_range_decode, codec::static_range, true, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, adaptive_range_encode, codec::adaptive_range, false, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, adaptive_range_decode, codec::adaptive_range, true, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, adaptive_range_multi_encode, codec::adaptive_range, false, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, adaptive_range_multi_decode, codec::adaptive_range, true, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, huffman_encode, codec::huffman, false, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, huffman_decode, codec::huffman, true, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, rans_encode, codec::rans, false, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, rans_decode, codec::rans, true, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, tans_encode, codec::tans, false, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, tans_decode, codec::tans, true, false)->Apply(archive_matrix);
BENCHMARK_CAPTURE(archive, static_range_multi_encode, codec::static_range, false, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, static_range_multi_decode, codec::static_range, true, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, huffman_multi_encode, codec::huffman, false, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, huffman_multi_decode, codec::huffman, true, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, rans_multi_encode, codec::rans, false, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, rans_multi_decode, codec::rans, true, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, tans_multi_encode, codec::tans, false, true)->Apply(multi_matrix);
BENCHMARK_CAPTURE(archive, tans_multi_decode, codec::tans, true, true)->Apply(multi_matrix);
}// namespace

int main(int argc, char **argv)
{
  // Corpus files retain their natural order and length. Register them only
  // when requested so an ordinary synthetic run needs no external files.
  if (auto const *directory = std::getenv("DACOEXEC_BENCHMARK_CORPUS")) {
    constexpr std::array names{ "static_range", "huffman", "rans", "tans", "adaptive_range", "binary" };
    for (std::size_t index = 0; index < names.size(); ++index) {
      for (auto const *dataset : { "text", "source", "binary", "structured" }) {
        for (bool decoding : { false, true }) {
          for (bool multi : { false, true }) {
            auto const name = std::string{ "archive_real/" } + names.at(index) + (multi ? "_multi" : "")
                              + (decoding ? "_decode/" : "_encode/") + dataset;
            benchmark::RegisterBenchmark(name.c_str(), [=](benchmark::State &state) {
              real_archive(
                state, static_cast<codec>(index), decoding, multi, std::filesystem::path(directory) / dataset);
            })->UseRealTime();
          }
        }
      }
    }
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) { return 1; }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
