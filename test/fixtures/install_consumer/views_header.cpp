#include <array>
#include <cstdint>
#include <dacoexec/views.hpp>
#include <ranges>
#include <vector>

[[nodiscard]] auto installed_views_round_trip() -> bool
{
  std::array<std::uint64_t, 3> input{ 1, 2, 13 };
  auto output = input | dacoexec::views::encode::gamma() | dacoexec::views::decode::gamma(input.size());
  std::vector<std::uint64_t> values;
  for (auto value : output) { values.push_back(value); }
  return std::ranges::equal(values, input);
}
