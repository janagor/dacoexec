//! \file
//! Single-pass numeric sequence operations with caller-owned bit I/O and storage.

#ifndef DACOEXEC_NUMERIC_SEQUENCE_HPP
#define DACOEXEC_NUMERIC_SEQUENCE_HPP

#include <dacoexec/numeric/delta.hpp>
#include <dacoexec/numeric/golomb.hpp>
#include <dacoexec/numeric/rice.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>
#include <dacoexec/bit/bit_reader.hpp>
#include <dacoexec/numeric/common.hpp>
#include <dacoexec/numeric/gamma.hpp>

namespace dacoexec::numeric {

/** Unsigned integer input, excluding bool, with values representable by uint64_t. */
template<class R>
concept unsigned_integer_range = std::ranges::input_range<R>
  && std::unsigned_integral<std::ranges::range_value_t<R>>
  && (!std::same_as<std::ranges::range_value_t<R>, bool>)
  && (std::numeric_limits<std::ranges::range_value_t<R>>::digits <= integer_bits);

/**
 * Consumes input once without copying or buffering the range. Does not finish out.
 * encode(out, value) must write one integer. Source, encoder and sink exceptions
 * propagate; accepted output remains and no rollback is attempted.
 */
template<bit_output Writer, unsigned_integer_range R, class Encode>
  requires std::invocable<Encode &, Writer &, std::uint64_t>
constexpr auto write_sequence(Writer &out, R &&input, Encode encode) -> void
{
  auto &&range = std::forward<R>(input);
  auto cursor = std::ranges::begin(range);
  auto const last = std::ranges::end(range);
  while (cursor != last) {
    using source_value = std::ranges::range_value_t<R>;
    std::invoke(encode, out, static_cast<std::uint64_t>(static_cast<source_value>(*cursor)));
    std::ranges::advance(cursor, 1);
  }
}

/**
 * Decodes exactly count integers into out and returns the advanced output cursor.
 * Does not require end-of-input: callers can append sequences to one bitstream.
 * Destination must have space and preserve uint64_t values. Reader, decoder and
 * sink exceptions propagate, retaining earlier output and any consumed input.
 */
template<std::output_iterator<std::uint64_t> Out, class Decode>
  requires std::invocable<Decode &, bit_reader &>
    && std::same_as<std::invoke_result_t<Decode &, bit_reader &>, std::uint64_t>
constexpr auto read_sequence(bit_reader &reader, std::size_t count, Out out, Decode decode) -> Out
{
  for (std::size_t index = 0; index < count; ++index) {
    auto const value = std::invoke(decode, reader);
    *out = value;
    std::ranges::advance(out, 1);
  }
  return out;
}

/** Writes positive integers using Gamma coding without finalizing out. */
template<bit_output Writer, unsigned_integer_range R>
constexpr auto write_gamma_sequence(Writer &out, R &&input) -> void
{
  write_sequence(out, std::forward<R>(input), [](auto &writer, std::uint64_t value) -> void {
    write_gamma(writer, value);
  });
}

template<std::output_iterator<std::uint64_t> Out>
constexpr auto read_gamma_sequence(bit_reader &reader, std::size_t count, Out out) -> Out
{
  return read_sequence(reader, count, std::move(out), [](bit_reader &input_reader) -> std::uint64_t {
    return read_gamma(input_reader);
  });
}

/** Writes positive integers using Delta coding without finalizing out. */
template<bit_output Writer, unsigned_integer_range R>
constexpr auto write_delta_sequence(Writer &out, R &&input) -> void
{
  write_sequence(out, std::forward<R>(input), [](auto &writer, std::uint64_t value) -> void {
    write_delta(writer, value);
  });
}

template<std::output_iterator<std::uint64_t> Out>
constexpr auto read_delta_sequence(bit_reader &reader, std::size_t count, Out out) -> Out
{
  return read_sequence(reader, count, std::move(out), [](bit_reader &input_reader) -> std::uint64_t {
    return read_delta(input_reader);
  });
}

/** Writes a golomb sequence; parameters are validated even for empty input. */
template<bit_output Writer, unsigned_integer_range R>
constexpr auto write_golomb_sequence(Writer &out, R &&input, std::uint32_t divisor) -> void
{
  detail::validate_golomb(divisor);
  write_sequence(out, std::forward<R>(input), [divisor](auto &writer, std::uint64_t value) -> void {
    write_golomb(writer, value, divisor);
  });
}

template<std::output_iterator<std::uint64_t> Out>
constexpr auto read_golomb_sequence(bit_reader &reader, std::size_t count, Out out, std::uint32_t divisor, decode_limits limits = {}) -> Out
{
  detail::validate_golomb(divisor);
  return read_sequence(reader, count, std::move(out), [divisor, limits](bit_reader &input_reader) -> std::uint64_t {
    return read_golomb(input_reader, divisor, limits);
  });
}

/** Writes a rice sequence; parameters are validated even for empty input. */
template<bit_output Writer, unsigned_integer_range R>
constexpr auto write_rice_sequence(Writer &out, R &&input, unsigned shift) -> void
{
  detail::validate_rice(shift);
  write_sequence(out, std::forward<R>(input), [shift](auto &writer, std::uint64_t value) -> void {
    write_rice(writer, value, shift);
  });
}

template<std::output_iterator<std::uint64_t> Out>
constexpr auto read_rice_sequence(bit_reader &reader, std::size_t count, Out out, unsigned shift, decode_limits limits = {}) -> Out
{
  detail::validate_rice(shift);
  return read_sequence(reader, count, std::move(out), [shift, limits](bit_reader &input_reader) -> std::uint64_t {
    return read_rice(input_reader, shift, limits);
  });
}

}// namespace dacoexec::numeric

#endif// DACOEXEC_NUMERIC_SEQUENCE_HPP
