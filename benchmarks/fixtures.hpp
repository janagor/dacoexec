#ifndef DACOEXEC_BENCHMARKS_FIXTURES_HPP
#define DACOEXEC_BENCHMARKS_FIXTURES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

namespace fixtures {
inline constexpr std::size_t kibibyte = 1024;
inline constexpr std::size_t mebibyte = kibibyte * kibibyte;
inline constexpr std::size_t stream_size = 64 * mebibyte;
inline constexpr std::uint32_t seed = 1729;
inline constexpr std::array block_sizes{ 64 * kibibyte, 256 * kibibyte, mebibyte, 4 * mebibyte };
inline constexpr std::array concurrency{ 1, 2, 4, 8 };
enum class dataset : std::uint8_t { uniform, repeated, alternating, skewed, text, alphabet };
inline constexpr std::array datasets{ dataset::uniform,
  dataset::repeated,
  dataset::alternating,
  dataset::skewed,
  dataset::text,
  dataset::alphabet };
inline constexpr std::array<std::string_view, datasets.size()> names{ "uniform",
  "repeated",
  "alternating",
  "skewed",
  "text",
  "alphabet" };

// Counter-based SplitMix64 bytes: fixed-seed, random-access, constant storage.
// Each group of eight bytes exposes one independently mixed counter value.
[[nodiscard]] constexpr auto uniform_byte(std::uint64_t index) -> std::byte
{
  constexpr std::uint64_t increment = 0x9e3779b97f4a7c15ULL;
  constexpr std::uint64_t first_multiplier = 0xbf58476d1ce4e5b9ULL;
  constexpr std::uint64_t second_multiplier = 0x94d049bb133111ebULL;
  constexpr unsigned first_shift = 30;
  constexpr unsigned second_shift = 27;
  constexpr unsigned final_shift = 31;
  constexpr unsigned byte_bits = 8;
  auto value = (index / sizeof(std::uint64_t)) + seed + increment;
  value = (value ^ (value >> first_shift)) * first_multiplier;
  value = (value ^ (value >> second_shift)) * second_multiplier;
  value ^= value >> final_shift;
  auto const shift = static_cast<unsigned>(index % sizeof(std::uint64_t)) * byte_bits;
  return static_cast<std::byte>(value >> shift);
}

// mt19937 output and explicit modulo avoid library-specific distribution algorithms.
inline auto make(std::size_t size, dataset kind) -> std::vector<std::byte>
{
  constexpr std::size_t alphabet_size = 256;
  constexpr std::size_t skew_period = 16;
  constexpr std::size_t alternating_symbols = 2;
  constexpr std::string_view text = "the quick brown fox jumps over the lazy dog. the data is ordered.\n";
  // Reproducible fixtures intentionally use a fixed seed.
  // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
  std::mt19937 random{ seed };
  std::vector<std::byte> result(size);
  for (std::size_t index = 0; index < size; ++index) {
    auto const value = random();
    unsigned char symbol = 0;
    switch (kind) {
    case dataset::uniform:
      symbol = static_cast<unsigned char>(value % alphabet_size);
      break;
    case dataset::repeated:
      symbol = 'a';
      break;
    case dataset::alternating:
      symbol = static_cast<unsigned char>(index % alternating_symbols);
      break;
    case dataset::skewed:
      symbol = value % skew_period == 0 ? static_cast<unsigned char>(value % alphabet_size) : 'a';
      break;
    case dataset::text:
      symbol = static_cast<unsigned char>(text.at(value % text.size()));
      break;
    case dataset::alphabet:
      symbol = static_cast<unsigned char>(index % alphabet_size);
      break;
    }
    result.at(index) = static_cast<std::byte>(symbol);
  }
  return result;
}
}// namespace fixtures
#endif// DACOEXEC_BENCHMARKS_FIXTURES_HPP
