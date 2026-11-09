//! \file
//! Complete ans archives over ordinary byte ranges; internal blocks never cross this interface.
#ifndef DACOEXEC_ANS_HPP
#define DACOEXEC_ANS_HPP
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/ans/coder.hpp>// IWYU pragma: export
#include <dacoexec/detail/ans/stream.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/facade_sink.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <limits>
#include <ostream>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace dacoexec::ans {
//! Maximum source bytes staged per frame. Valid block sizes are 1 through 16 MiB.
struct stream_options
{
  std::size_t block_size_ = ::dacoexec::detail::ans::default_block_size;
  entropy_coder coder_ = entropy_coder::rans;
  unsigned table_log_ = ::dacoexec::detail::ans::tans::default_table_log;
};

//! Bounds checked before publishing each decoded frame. Zero total output permits empty archives only.
struct decode_options
{
  std::size_t max_frame_size_ = ::dacoexec::detail::ans::max_frame_size;
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
};

//! Successful operation counts; archive byte counts include headers and checksums.
struct stream_statistics
{
  std::uint64_t input_bytes_ = 0;
  std::uint64_t output_bytes_ = 0;
  std::uint64_t frames_ = 0;
};

//! One-pass compression. Destination is an output iterator, ostream, or object with void write(span<const byte>).
//! Input may contain byte, char, signed/unsigned char, or char8_t values, including single-pass ranges.
//! Memory is bounded by the block size. Source and destination failures propagate; accepted writes remain.
template<std::ranges::viewable_range R, class Destination>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
    && (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
    || std::output_iterator<std::remove_cvref_t<Destination>, std::byte>
    || requires(Destination &sink, std::span<std::byte const> bytes) { { sink.write(bytes) } -> std::same_as<void>; })
[[nodiscard]] auto compress_stream(R &&source, Destination &&destination, stream_options options = {})
  -> stream_statistics
{
  auto statistics = ::dacoexec::detail::with_byte_sink(std::forward<Destination>(destination), [&](auto &sink) -> auto {
    return ::dacoexec::detail::ans::compress_stream(std::forward<R>(source),
      sink,
      { .block_size_ = options.block_size_, .coder_ = options.coder_, .table_log_ = options.table_log_ });
  });
  return { statistics.input_bytes_, statistics.output_bytes_, statistics.frames_ };
}

//! One-pass verified decompression. Each frame is validated before its output is written.
//! Missing END, trailing input, bad checksums, and configured limits throw. Earlier writes remain on failure.
template<std::ranges::viewable_range R, class Destination>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
    && (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
    || std::output_iterator<std::remove_cvref_t<Destination>, std::byte>
    || requires(Destination &sink, std::span<std::byte const> bytes) { { sink.write(bytes) } -> std::same_as<void>; })
[[nodiscard]] auto decompress_stream(R &&source, Destination &&destination, decode_options options = {})
  -> stream_statistics
{
  auto statistics = ::dacoexec::detail::with_byte_sink(std::forward<Destination>(destination), [&](auto &sink) -> auto {
    return ::dacoexec::detail::ans::decompress_stream(std::forward<R>(source),
      sink,
      { .max_frame_size_ = options.max_frame_size_, .max_output_bytes_ = options.max_output_bytes_ });
  });
  return { statistics.input_bytes_, statistics.output_bytes_, statistics.frames_ };
}

//! Returns a complete archive. Empty input produces a valid empty archive.
template<std::ranges::viewable_range R>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress(R &&source, stream_options options = {}) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  static_cast<void>(compress_stream(std::forward<R>(source), std::back_inserter(result), options));
  return result;
}

//! Returns verified bytes from one complete archive. No result is returned on failure.
template<std::ranges::viewable_range R>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress(R &&source, decode_options options = {}) -> std::vector<std::byte>
{
  std::vector<std::byte> result;
  static_cast<void>(decompress_stream(std::forward<R>(source), std::back_inserter(result), options));
  return result;
}
}// namespace dacoexec::ans

#endif// DACOEXEC_ANS_HPP
