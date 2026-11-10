//! \file
//! Optional scheduled parallel compression and verified decompression.

#ifndef DACOEXEC_HUFFMAN_PARALLEL_EXECUTION_HPP
#define DACOEXEC_HUFFMAN_PARALLEL_EXECUTION_HPP

#include <dacoexec/detail/codec/parallel_execution.hpp>
#include <dacoexec/detail/execution/ordered_parallel.hpp>
#include <dacoexec/detail/huffman/stream.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
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

struct parallel_format : stream_format
{
  using options_type = parallel_options;
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

//! Frame/output limits and positive maximum number of frames per decode batch.
struct parallel_decode_options
{
  std::size_t max_frame_size_ = max_frame_size;
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
  std::size_t max_in_flight_ = execution::default_max_in_flight;
};

//! Lazy single-use decoder. Owns rvalue input and borrows lvalue input and sink.
//! Parses sequentially, reserves the cumulative output budget before decoding,
//! and decodes/verifies owned frames concurrently. A batch is written in order
//! only after every frame passes verification. Failed/stopped batches are not
//! written; earlier batches remain. Sink failures may leave partial output.
//! END and exact EOF are required. Parsing errors discard the acquiring batch.
//! Work drains before error/stopped completion. Limits are validated on start.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decode_parallel_on(Scheduler scheduler, R &&source, S &sink, parallel_decode_options options = {})
{
  decode_options limits{ .max_frame_size_ = options.max_frame_size_, .max_output_bytes_ = options.max_output_bytes_ };
  return execution::ordered_parallel_on(std::move(scheduler),
    codec::parallel_decode_source<std::views::all_t<R>, S, stream_format>{
      std::views::all(std::forward<R>(source)), &sink, limits },
    codec::frame_decoder<stream_format>{ limits },
    options.max_in_flight_);
}

}// namespace dacoexec::detail::huffman

#endif// DACOEXEC_HUFFMAN_PARALLEL_EXECUTION_HPP
