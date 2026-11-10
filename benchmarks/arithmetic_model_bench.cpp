#include <benchmark/benchmark.h>
#include <dacoexec/detail/arithmetic/static_range_codec.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
constexpr std::size_t slot_count = 1U << 12U;

// The histogram varies alphabet size and skew; queried slots are identical for
// both strategies and distributed as cumulative slots from valid intervals.
auto make_model(unsigned alphabet, bool skewed) -> arithmetic::static_model
{
  dacoexec::detail::model::frequency_table counts{};
  for (unsigned symbol = 0; symbol < alphabet; ++symbol) { counts.at(symbol) = 1; }
  if (skewed) { counts.front() = 4096; }
  return dacoexec::detail::model::make_model(counts);
}

void lookup(benchmark::State &state, bool direct)
{
  auto const model = make_model(static_cast<unsigned>(state.range(0)), state.range(1) != 0);
  auto const table = arithmetic::make_symbol_lookup(model);
  std::mt19937 random(42);
  std::vector<std::uint32_t> slots(65536);
  for (auto &slot : slots) { slot = static_cast<std::uint32_t>(random() % slot_count); }
  for (auto unused : state) {
    benchmark::DoNotOptimize(slots.data());
    benchmark::ClobberMemory();
    std::uint64_t checksum = 0;
    if (direct) {
      for (auto slot : slots) { checksum += table.at(slot); }
    } else {
      for (auto slot : slots) { checksum += arithmetic::find_symbol(model, slot); }
    }
    benchmark::DoNotOptimize(checksum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(slots.size()));
}

void build_lookup(benchmark::State &state)
{
  auto const model = make_model(static_cast<unsigned>(state.range(0)), state.range(1) != 0);
  for (auto unused : state) {
    auto table = arithmetic::make_symbol_lookup(model);
    benchmark::DoNotOptimize(table);
  }
}

auto decode_direct(std::span<std::byte const> payload, arithmetic::static_model const &model, std::size_t count)
  -> std::vector<std::byte>
{
  // Include table allocation/construction in the timed complete-block path.
  auto const table = arithmetic::make_symbol_lookup(model);
  arithmetic::range_decoder decoder(payload);
  std::vector<std::byte> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto const symbol = table.at(decoder.cumulative(model.total()));
    decoder.decode(model.cumulative().at(symbol), model.frequencies().at(symbol), model.total());
    result.push_back(static_cast<std::byte>(symbol));
  }
  decoder.finish();
  return result;
}

void block_decode(benchmark::State &state, bool direct)
{
  auto const count = static_cast<std::size_t>(state.range(0));
  auto const alphabet = static_cast<unsigned>(state.range(1));
  auto const precision = static_cast<unsigned>(state.range(2));
  std::mt19937 random(42);
  std::vector<std::byte> input(count);
  for (auto &value : input) { value = static_cast<std::byte>(random() % alphabet); }
  auto const model = arithmetic::make_static_model(input, precision);
  auto const payload = arithmetic::encode_block(input, model);
  if (arithmetic::decode_block(payload, model, count, count) != input
      || decode_direct(payload, model, count) != input) {
    state.SkipWithError("Decoder strategies disagree with input");
    return;
  }
  for (auto unused : state) {
    auto output =
      direct ? decode_direct(payload, model, count) : arithmetic::decode_block(payload, model, count, count);
    benchmark::DoNotOptimize(output);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

BENCHMARK_CAPTURE(lookup, cumulative_search, false)->ArgsProduct({ { 2, 16, 256 }, { 0, 1 } });
BENCHMARK_CAPTURE(lookup, direct_table, true)->ArgsProduct({ { 2, 16, 256 }, { 0, 1 } });
BENCHMARK(build_lookup)->ArgsProduct({ { 2, 16, 256 }, { 0, 1 } });
BENCHMARK_CAPTURE(block_decode, cumulative_search, false)
  ->ArgsProduct({ { 64, 4096, 65536 }, { 2, 256 }, { 8, 12, 16 } });
BENCHMARK_CAPTURE(block_decode, direct_table, true)->ArgsProduct({ { 64, 4096, 65536 }, { 2, 256 }, { 8, 12, 16 } });
}// namespace

BENCHMARK_MAIN();
