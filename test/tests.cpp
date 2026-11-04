#include <catch2/catch_test_macros.hpp>

#include <dacoexec/dacoexec.hpp> // IWYU pragma: keep -- Public umbrella smoke test.

#include <array>
#include <cstddef>
#include <ranges>
#include <span>

TEST_CASE("The public header is available to C++20 code", "[infrastructure]")
{
  constexpr std::array<std::byte, 1> bytes{ std::byte{ 0 } };
  std::span<std::byte const> const view{ bytes };

  REQUIRE(std::ranges::size(view) == 1);
  REQUIRE(std::ranges::data(view) != nullptr);
}
