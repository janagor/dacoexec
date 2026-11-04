#ifndef DACOEXEC_HUFFMAN_EXECUTION_HPP
#define DACOEXEC_HUFFMAN_EXECUTION_HPP

#include <dacoexec/execution/ordered_parallel.hpp>
#include <dacoexec/huffman/stream.hpp>

#include <cstddef>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dacoexec::huffman {
struct parallel_options
{
  std::size_t block_size_ = default_block_size;
  std::size_t max_in_flight_ = execution::default_max_in_flight;
};
namespace detail {
  template<std::ranges::view V, io::byte_output_sink Sink>
  struct parallel_source
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
      : source_(std::move(source)), sink_(sink), options_(options) {}

    auto initialize() -> void
    {
      if (options_.block_size_ == 0 || options_.block_size_ > max_frame_size) {
        throw std::invalid_argument("Invalid block size");
      }
      blocks_.emplace(std::move(source_), options_.block_size_);
      write_archive_header(*sink_);
    }
    auto next() -> std::optional<byte_block>
    {
      if (!cursor_) { cursor_.emplace(blocks_->begin()); }
      else if (advance_) { ++*cursor_; }
      advance_ = false;
      if (*cursor_ == blocks_->end()) { return std::nullopt; }
      advance_ = true;
      return std::optional<byte_block>{ std::in_place, std::move(**cursor_) };
    }
    auto commit(encoded_frame frame) -> void
    {
      checked_add(statistics_.input_bytes_, frame.header_.uncompressed_size_);
      checked_add(statistics_.output_bytes_, frame_header_size + frame.block_.payload_.size()
        + (frame.header_.kind_ == frame_kind::huffman ? alphabet_size : 0U));
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
    auto operator()(byte_block block) const -> encoded_frame { return encode_frame(std::move(block)); }
  };
}// namespace detail

// Lazy, single-use sender. Rvalue ranges are owned; lvalue ranges are borrowed.
// The sink and scheduler execution context must outlive completion. Source and
// sink access is serialized, but can migrate to the last completing worker.
// A sink failure can leave a partial archive; no writes are retried.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto encode_parallel_on(Scheduler scheduler, R &&source, S &sink, parallel_options options = {})
  -> execution::detail::ordered_sender<Scheduler, detail::parallel_source<std::views::all_t<R>, S>, detail::frame_encoder>
{
  return execution::ordered_parallel_on(std::move(scheduler),
    detail::parallel_source<std::views::all_t<R>, S>{ std::views::all(std::forward<R>(source)), &sink, options },
    detail::frame_encoder{}, options.max_in_flight_);
}
}// namespace dacoexec::huffman
#endif // DACOEXEC_HUFFMAN_EXECUTION_HPP
