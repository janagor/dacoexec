//! \file
//! Byte frequency counting with checked counters.

#ifndef DACOEXEC_MODEL_FREQUENCY_HPP
#define DACOEXEC_MODEL_FREQUENCY_HPP

#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <dacoexec/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::model {

/**
 * Number of distinct byte values. Index 0 is the byte 0x00 and index 255 is 0xFF.
 */
inline constexpr std::size_t alphabet_size = 256;

/**
 * Occurrence count of every byte value. A zero entry means that symbol is absent.
 */
using frequency_table = std::array<std::uint64_t, alphabet_size>;

/**
 * Add one count for each byte in `input`.
 *
 * The range is read once and is not copied or buffered. Move-only, unsized,
 * and single-pass ranges are accepted. Each element is converted with the
 * same unsigned-byte rules as the rest of the library, including a proxy
 * whose value_type is a supported byte type. Signed character values keep
 * the low eight bits, so 0x00, 0x80, and 0xFF stay distinct.
 *
 * A counter stops at the maximum std::uint64_t. The next occurrence of that
 * symbol throws dacoexec::exception (errc::numeric_overflow). An exception from the source, or that
 * overflow, leaves earlier increments applied.
 *
 * @param frequencies Existing counters to mutate without clearing them.
 * @param input Byte input range consumed once.
 * @throws dacoexec::exception (errc::numeric_overflow) If an increment cannot fit in std::uint64_t.
 * @see count_frequencies
 */
template<byte_input_range R> constexpr auto accumulate_frequencies(frequency_table &frequencies, R &&input) -> void
{
  auto &&range = std::forward<R>(input);
  auto cursor = std::ranges::begin(range);
  auto const last = std::ranges::end(range);
  while (cursor != last) {
    using source_value = std::ranges::range_value_t<R>;
    std::byte const value = dacoexec::detail::support::to_byte(static_cast<source_value>(*cursor));
    std::size_t const symbol = std::to_integer<unsigned char>(value);
    auto &count = frequencies.at(symbol);
    if (count == std::numeric_limits<std::uint64_t>::max()) {
      throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "Byte frequency counter overflow");
    }
    ++count;
    std::ranges::advance(cursor, 1);
  }
}

/**
 * Count byte symbols in `input`.
 *
 * This is accumulate_frequencies on a zeroed table. The caller receives a
 * finished table, or the function throws and no table is returned.
 *
 * @return Counts indexed by unsigned byte value.
 * @throws dacoexec::exception (errc::numeric_overflow) If a symbol count cannot fit in std::uint64_t.
 * Source exceptions propagate.
 * @see accumulate_frequencies
 */
template<byte_input_range R> [[nodiscard]] constexpr auto count_frequencies(R &&input) -> frequency_table
{
  frequency_table frequencies{};
  ::dacoexec::detail::model::accumulate_frequencies(frequencies, std::forward<R>(input));
  return frequencies;
}

}// namespace dacoexec::detail::model

#endif// DACOEXEC_MODEL_FREQUENCY_HPP
