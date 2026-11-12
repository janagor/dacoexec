#include <benchmark/benchmark.h>
#include <dacoexec/detail/arithmetic/range/adaptive/fenwick_model.hpp>
#include <dacoexec/detail/arithmetic/range/adaptive/model.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
enum class distribution : std::uint8_t { zero, ff, uniform, skewed };
enum class operation : std::uint8_t { updates, lookup, interval_lookup, encode_transition, decode_transition };
constexpr std::array names{ "zero", "ff", "uniform", "skewed" };

auto make_input(std::size_t count, distribution pattern) -> std::vector<std::uint8_t>
{
  std::mt19937 random(1729);
  std::vector<std::uint8_t> input(count);
  for (auto &symbol : input) {
    switch (pattern) {
    case distribution::zero:
      symbol = 0;
      break;
    case distribution::ff:
      symbol = 255;
      break;
    case distribution::uniform:
      symbol = static_cast<std::uint8_t>(random() % 256);
      break;
    case distribution::skewed:
      symbol = random() % 16 == 0 ? static_cast<std::uint8_t>(random() % 256) : std::uint8_t{ 0 };
      break;
    }
  }
  return input;
}

// Build a changing-model slot trace with the reference representation. Check
// the candidate's intervals, reverse lookup, updates and rescaling before timing.
// Every strategy receives the same slot sequence, sampled with seed 42.
template<class Model> auto validate_and_make_trace(std::vector<std::uint8_t> const &input) -> std::vector<std::uint32_t>
{
  arithmetic::adaptive_model reference;
  Model candidate;
  std::mt19937 random(42);
  std::vector<std::uint32_t> slots;
  slots.reserve(input.size());
  for (auto symbol : input) {
    auto const expected = reference.interval_for(symbol);
    auto const actual = candidate.interval_for(symbol);
    auto const slot = expected.cumulative_ + static_cast<std::uint32_t>(random() % expected.frequency_);
    if (actual.cumulative_ != expected.cumulative_ || actual.frequency_ != expected.frequency_
        || actual.total_ != expected.total_ || candidate.find_symbol(slot) != symbol) {
      throw std::runtime_error("Adaptive model disagrees with reference transition");
    }
    slots.push_back(slot);
    reference.update(symbol);
    candidate.update(symbol);
  }
  if (candidate.frequencies() != reference.frequencies() || candidate.total() != reference.total()) {
    throw std::runtime_error("Adaptive model disagrees with reference final state");
  }
  return slots;
}

void report_symbols(benchmark::State &state, std::size_t count)
{
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
  state.counters["seconds_per_symbol"] = benchmark::Counter(
    static_cast<double>(count), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

template<class Model, operation Op> void model_operation(benchmark::State &state, distribution pattern)
{
  auto const input = make_input(static_cast<std::size_t>(state.range(0)), pattern);
  std::vector<std::uint32_t> slots;
  Model initial;
  try {
    slots = validate_and_make_trace<Model>(input);
    if constexpr (Op == operation::lookup || Op == operation::interval_lookup) {
      // Keep isolated lookup costs on a frozen, fully trained model.
      arithmetic::adaptive_model reference;
      for (auto symbol : input) {
        initial.update(symbol);
        reference.update(symbol);
      }
      std::mt19937 random(42);
      for (std::size_t index = 0; index < input.size(); ++index) {
        auto const symbol = input.at(index);
        auto const expected = reference.interval_for(symbol);
        auto const actual = initial.interval_for(symbol);
        auto const slot = expected.cumulative_ + static_cast<std::uint32_t>(random() % expected.frequency_);
        if (actual.cumulative_ != expected.cumulative_ || actual.frequency_ != expected.frequency_
            || actual.total_ != expected.total_ || initial.find_symbol(slot) != symbol) {
          throw std::runtime_error("Adaptive model disagrees with reference frozen interval");
        }
        slots.at(index) = slot;
      }
    }
  } catch (std::exception const &error) {
    state.SkipWithError(error.what());
    return;
  }
  state.SetLabel(names.at(static_cast<std::size_t>(pattern)));
  Model model = initial;
  for (auto unused : state) {
    if constexpr (Op == operation::updates || Op == operation::encode_transition
                  || Op == operation::decode_transition) {
      state.PauseTiming();
      model = initial;
      state.ResumeTiming();
    }
    benchmark::DoNotOptimize(input.data());
    benchmark::DoNotOptimize(slots.data());
    benchmark::DoNotOptimize(model);
    benchmark::ClobberMemory();
    std::uint64_t checksum = 0;
    if constexpr (Op == operation::updates) {
      for (auto symbol : input) { model.update(symbol); }
    } else if constexpr (Op == operation::lookup) {
      for (auto slot : slots) { checksum += model.find_symbol(slot); }
    } else if constexpr (Op == operation::interval_lookup || Op == operation::encode_transition) {
      for (auto symbol : input) {
        auto const interval = model.interval_for(symbol);
        checksum += interval.cumulative_ + interval.frequency_ + interval.total_;
        if constexpr (Op == operation::encode_transition) { model.update(symbol); }
      }
    } else {
      for (auto slot : slots) {
        auto const symbol = model.find_symbol(slot);
        auto const interval = model.interval_for(symbol);
        checksum += symbol + interval.cumulative_ + interval.frequency_ + interval.total_;
        model.update(symbol);
      }
    }
    benchmark::DoNotOptimize(checksum);
    benchmark::DoNotOptimize(model);
    benchmark::ClobberMemory();
  }
  report_symbols(state, input.size());
}

template<class Model, operation Op> void register_operation(std::string const &model_name, std::string const &op_name)
{
  for (std::size_t pattern = 0; pattern < names.size(); ++pattern) {
    auto const name = model_name + "/" + op_name + "/" + names.at(pattern);
    benchmark::RegisterBenchmark(name.c_str(), &model_operation<Model, Op>, static_cast<distribution>(pattern))
      ->Arg(65536)
      ->Arg(1048576);
  }
}

template<class Model> void register_model(std::string const &name)
{
  register_operation<Model, operation::updates>(name, "updates");
  register_operation<Model, operation::lookup>(name, "lookup");
  register_operation<Model, operation::interval_lookup>(name, "interval_lookup");
  register_operation<Model, operation::encode_transition>(name, "encode_transition");
  register_operation<Model, operation::decode_transition>(name, "decode_transition");
}

[[maybe_unused]] auto const registered = [] {
  register_model<arithmetic::adaptive_model>("array");
  register_model<arithmetic::fenwick_adaptive_model>("fenwick");
  return true;
}();
}// namespace

BENCHMARK_MAIN();
