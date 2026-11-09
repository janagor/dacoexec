//! \file
//! Optional scheduled parallel compression with ordered frame writes.

#ifndef DACOEXEC_HUFFMAN_PARALLEL_COMPRESSION_HPP
#define DACOEXEC_HUFFMAN_PARALLEL_COMPRESSION_HPP

#include <dacoexec/detail/codec/parallel_source.hpp>
#include <dacoexec/detail/execution/ordered_parallel.hpp>
#include <dacoexec/detail/huffman/stream.hpp>

#include <cstddef>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::huffman {

/**
 * Block size and batch bound for encode_parallel_on().
 */
struct parallel_options
{
  //! Maximum uncompressed bytes per frame, from one through max_frame_size.
  std::size_t block_size_ = default_block_size;
  //! Positive maximum number of owned blocks in one processing batch.
  std::size_t max_in_flight_ = execution::default_max_in_flight;
};

struct parallel_format
{
  using frame_type = encoded_frame;
  using options_type = parallel_options;
  static constexpr auto archive_header_size = huffman::archive_header_size;
  static constexpr auto frame_header_size = huffman::frame_header_size;
  static constexpr auto max_frame_size = huffman::max_frame_size;

  static auto validate_options(parallel_options const & /*options*/) -> void {}

  [[nodiscard]] static auto metadata_size(encoded_frame const &frame) -> std::size_t
  { return frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U; }

  template<io::byte_output_sink Sink> static auto write_archive_header(Sink &sink) -> void
  { huffman::write_archive_header(sink); }

  template<io::byte_output_sink Sink> static auto write_frame(Sink &sink, encoded_frame const &frame) -> void
  { huffman::write_frame(sink, frame); }

  template<io::byte_output_sink Sink> static auto write_end(Sink &sink) -> void
  { huffman::write_frame_header(sink, {}); }
};

template<std::ranges::view V, io::byte_output_sink Sink>
using parallel_source = codec::parallel_source<V, Sink, parallel_format>;

struct frame_encoder
{
  auto operator()(byte_block block) const -> encoded_frame { return encode_frame(std::move(block)); }
};

/**
 * Lazy, single-use sender. Rvalue ranges are owned; lvalue ranges are borrowed.
 * The sink and scheduler execution context must outlive completion. Source and
 * sink access is serialized, but can migrate to the last completing worker.
 * A sink failure can leave a partial archive; no writes are retried.
 *
 * Blocks are encoded concurrently in bounded batches and committed in input
 * order. Successful completion returns stream_statistics with the same counts
 * as compress_stream(). Errors complete with std::exception_ptr; cancellation
 * completes stopped after outstanding work drains. Previously written bytes
 * remain after error or cancellation. END is written only on successful finish.
 * Invalid block size or zero batch capacity is reported when started.
 *
 * @param scheduler Scheduler used for block encoding.
 * @param source Viewable byte input range consumed once when started.
 * @param sink Borrowed destination that must outlive completion.
 * @param options Block and batch bounds.
 * @see execution::ordered_parallel_on
 */
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto encode_parallel_on(Scheduler scheduler, R &&source, S &sink, parallel_options options = {})
  -> execution::ordered_sender<Scheduler,
    ::dacoexec::detail::huffman::parallel_source<std::views::all_t<R>, S>,
    ::dacoexec::detail::huffman::frame_encoder>
{
  return execution::ordered_parallel_on(std::move(scheduler),
    ::dacoexec::detail::huffman::parallel_source<std::views::all_t<R>, S>{
      std::views::all(std::forward<R>(source)), &sink, options },
    ::dacoexec::detail::huffman::frame_encoder{},
    options.max_in_flight_);
}

}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_PARALLEL_COMPRESSION_HPP
