//! \file
//! Optional scheduled parallel compression with ordered frame writes.

#ifndef DACOEXEC_ANS_EXECUTION_HPP
#define DACOEXEC_ANS_EXECUTION_HPP

#include <dacoexec/detail/ans/stream.hpp>
#include <dacoexec/detail/execution/ordered_parallel.hpp>

#include <cstddef>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::ans {

/**
 * Block size and batch bound for encode_parallel_on().
 */
struct parallel_options
{
  //! Maximum uncompressed bytes per frame, from one through max_frame_size.
  std::size_t block_size_ = default_block_size;
  //! Positive maximum number of owned blocks in one processing batch.
  std::size_t max_in_flight_ = execution::default_max_in_flight;
  entropy_coder coder_ = entropy_coder::rans;
  unsigned table_log_ = tans::default_table_log;
};

template<std::ranges::view V, io::byte_output_sink Sink> struct parallel_source
{
  using input_type = byte_block;
  using result_type = encoded_frame;
  V source_;
  Sink *sink_;
  parallel_options options_;
  std::optional<blocks_view<V>> blocks_;
  std::optional<typename blocks_view<V>::iterator> cursor_;
  bool advance_ = false;
  stream_statistics statistics_{ .output_bytes_ = archive_header_size + frame_header_size };

  parallel_source(V source, Sink *sink, parallel_options options)
    : source_(std::move(source)), sink_(sink), options_(options)
  {}

  auto initialize() -> void
  {
    if (options_.block_size_ == 0 || options_.block_size_ > max_frame_size) {
      throw std::invalid_argument("Invalid block size");
    }
    validate_coder(options_.coder_, options_.table_log_);
    blocks_.emplace(std::move(source_), options_.block_size_);
    write_archive_header(*sink_);
  }

  auto next() -> std::optional<byte_block>
  {
    if (!blocks_) { throw std::logic_error("Parallel source is not initialized"); }
    if (!cursor_) {
      cursor_.emplace(blocks_->begin());
    } else if (advance_) {
      ++*cursor_;
    }
    advance_ = false;
    if (!cursor_) { throw std::logic_error("Parallel source cursor is not initialized"); }
    if (*cursor_ == blocks_->end()) { return std::nullopt; }
    advance_ = true;
    return std::optional<byte_block>{ std::in_place, std::move(**cursor_) };
  }

  auto commit(encoded_frame frame) -> void
  {
    checked_add(statistics_.input_bytes_, frame.header_.uncompressed_size_);
    checked_add(statistics_.output_bytes_,
      frame_header_size + frame.block_.payload_.size() + ::dacoexec::detail::ans::model_metadata_size(frame.block_));
    checked_add(statistics_.frames_, 1);
    write_frame(*sink_, frame);
  }

  auto finish() -> stream_statistics
  {
    write_frame_header(*sink_, {});
    return statistics_;
  }
};

struct frame_encoder
{
  entropy_coder coder_ = entropy_coder::rans;
  unsigned table_log_ = tans::default_table_log;

  auto operator()(byte_block block) const -> encoded_frame
  { return encode_frame(std::move(block), coder_, table_log_); }
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
    ::dacoexec::detail::ans::parallel_source<std::views::all_t<R>, S>,
    ::dacoexec::detail::ans::frame_encoder>
{
  return execution::ordered_parallel_on(std::move(scheduler),
    ::dacoexec::detail::ans::parallel_source<std::views::all_t<R>, S>{
      std::views::all(std::forward<R>(source)), &sink, options },
    ::dacoexec::detail::ans::frame_encoder{ options.coder_, options.table_log_ },
    options.max_in_flight_);
}

}// namespace dacoexec::detail::ans

#endif// DACOEXEC_ANS_EXECUTION_HPP
