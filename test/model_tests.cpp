#include "error_test_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <dacoexec/detail/model/frequency_model.hpp>
#include <dacoexec/detail/support/byte.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

TEST_CASE("Frequency normalization preserves support and handles full-width counts", "[model]")
{
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  auto const model = dacoexec::detail::model::make_model(std::array<std::uint64_t, 2>{ maximum - 1, 1 });
  REQUIRE(model.frequencies().front() == model.total() - 1);
  REQUIRE(model.frequencies().back() == 1);
  REQUIRE_THROWS_MATCHES((dacoexec::detail::model::make_model(std::array<std::uint64_t, 2>{ maximum, 1 })),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::numeric_overflow));
  REQUIRE_THROWS_MATCHES((dacoexec::detail::model::make_model(std::array<std::uint64_t, 2>{ 0, 0 })),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE_THROWS_MATCHES((dacoexec::detail::model::make_model(std::array<std::uint64_t, 2>{ 1, 1 }, 0)),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  REQUIRE_THROWS_MATCHES((dacoexec::detail::model::make_model(
                           std::array<std::uint64_t, 2>{ 1, 1 }, dacoexec::detail::model::max_precision_bits + 1)),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  dacoexec::detail::model::frequency_table counts{};
  counts.fill(1);
  REQUIRE_THROWS_MATCHES(dacoexec::detail::model::make_model(counts, 1),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
  for (unsigned precision = dacoexec::detail::support::bits_per_byte;
    precision <= dacoexec::detail::model::max_precision_bits;
    ++precision) {
    auto const normalized = dacoexec::detail::model::make_model(counts, precision);
    std::uint32_t sum = 0;
    for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
      REQUIRE(normalized.cumulative().at(symbol) == sum);
      REQUIRE(normalized.frequencies().at(symbol) > 0);
      sum += normalized.frequencies().at(symbol);
    }
    REQUIRE(sum == normalized.total());
    REQUIRE(dacoexec::detail::model::make_model(counts, precision).frequencies() == normalized.frequencies());
  }
}

TEST_CASE("Frequency models reject inconsistent totals", "[model]")
{
  using model_type = dacoexec::detail::model::frequency_model<dacoexec::detail::model::alphabet_size>;
  REQUIRE_THROWS_MATCHES(model_type(model_type::table_type{}, dacoexec::detail::model::default_precision_bits),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_argument));
}
