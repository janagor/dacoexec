//! \file
//! Internal bounded buffer growth without capacity overflow.

#ifndef DACOEXEC_DETAIL_BUFFER_HPP
#define DACOEXEC_DETAIL_BUFFER_HPP

#include <cstddef>
#include <limits>

namespace dacoexec::detail {

  inline constexpr std::size_t block_growth_origin = 4096;

  // Next vector capacity while filling a bounded buffer. The first reservation
  // is at most 4096 bytes, later steps double, and the result never exceeds
  // `limit`. Doubling is skipped when it would overflow size_t.
  [[nodiscard]] constexpr auto next_block_capacity(std::size_t capacity, std::size_t limit) noexcept -> std::size_t
  {
    if (capacity >= limit) { return limit; }
    if (capacity == 0) { return limit < block_growth_origin ? limit : block_growth_origin; }
    if (capacity > (std::numeric_limits<std::size_t>::max() / 2)) { return limit; }
    std::size_t const doubled = capacity * 2;
    return doubled < limit ? doubled : limit;
  }

}// namespace dacoexec::detail

#endif // DACOEXEC_DETAIL_BUFFER_HPP
