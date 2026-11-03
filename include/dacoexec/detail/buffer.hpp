#pragma once

#include <cstddef>
#include <limits>

namespace dacoexec {
namespace detail {

  inline constexpr std::size_t block_growth_origin = 4096;

  // Next vector capacity while filling a bounded buffer. The first reservation
  // is at most 4096 bytes, later steps double, and the result never exceeds
  // `limit`. Doubling is skipped when it would overflow size_t.
  [[nodiscard]] constexpr std::size_t next_block_capacity(std::size_t capacity, std::size_t limit) noexcept
  {
    if (capacity >= limit) { return limit; }
    if (capacity == 0) { return limit < block_growth_origin ? limit : block_growth_origin; }
    if (capacity > (std::numeric_limits<std::size_t>::max() / 2)) { return limit; }
    std::size_t const doubled = capacity * 2;
    return doubled < limit ? doubled : limit;
  }

}// namespace detail
}// namespace dacoexec
