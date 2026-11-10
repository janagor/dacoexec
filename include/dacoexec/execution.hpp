//! \file
//! Optional lazy parallel archive compression and decompression; requires dacoexec::execution and stdexec.
#ifndef DACOEXEC_EXECUTION_HPP
#define DACOEXEC_EXECUTION_HPP
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <dacoexec/ans.hpp>
#include <dacoexec/detail/ans/parallel_execution.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/huffman/parallel_execution.hpp>
#include <dacoexec/huffman.hpp>
#include <functional>
#include <iterator>
#include <limits>
#include <ostream>
#include <ranges>
#include <span>
#include <stdexec/execution.hpp>
#include <type_traits>
#include <utility>

namespace dacoexec::huffman {
struct parallel_options
{
  std::size_t block_size_ = ::dacoexec::detail::huffman::default_block_size;
  std::size_t max_in_flight_ = ::dacoexec::detail::execution::default_max_in_flight;
};

//! Returns a lazy single-use sender completing with public stream_statistics.
//! Lvalue sources are borrowed; rvalue sources are owned. Borrowed sources, sinks,
//! ostream objects, and scheduler contexts must outlive completion.
//! Writes are ordered; failures and cancellation drain in-flight work. Earlier writes remain.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, class Destination>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
    && (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
    || std::output_iterator<std::remove_cvref_t<Destination>, std::byte>
    || requires(Destination &sink, std::span<std::byte const> bytes) { { sink.write(bytes) } -> std::same_as<void>; })
[[nodiscard]] auto
  compress_parallel_on(Scheduler scheduler, R &&source, Destination &&destination, parallel_options options = {})
{
  auto source_view = std::views::all(std::forward<R>(source));
  auto make_sender = [scheduler = std::move(scheduler), source = std::move(source_view), options](
                       auto &sink) mutable -> auto {
    return stdexec::then(::dacoexec::detail::huffman::encode_parallel_on(std::move(scheduler),
                           std::move(source),
                           sink,
                           { .block_size_ = options.block_size_, .max_in_flight_ = options.max_in_flight_ }),
      [](auto statistics) -> stream_statistics {
        return { statistics.input_bytes_, statistics.output_bytes_, statistics.frames_ };
      });
  };
  if constexpr (std::derived_from<std::remove_reference_t<Destination>, std::ostream>) {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel streams must be lvalues");
    return stdexec::let_value(stdexec::just(::dacoexec::detail::io::ostream_sink(destination)), std::move(make_sender));
  } else if constexpr (std::output_iterator<std::remove_cvref_t<Destination>, std::byte>) {
    return stdexec::let_value(
      stdexec::just(::dacoexec::detail::io::iterator_sink(std::forward<Destination>(destination))),
      std::move(make_sender));
  } else {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel sinks must be lvalues");
    return std::invoke(std::move(make_sender), destination);
  }
}

//! Limits checked before decoded output allocation; positive decode batch capacity.
struct parallel_decode_options
{
  std::size_t max_frame_size_ = ::dacoexec::detail::huffman::max_frame_size;
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
  std::size_t max_in_flight_ = ::dacoexec::detail::execution::default_max_in_flight;
};

//! Lazy single-use verified decompression returning public stream_statistics.
//! Frames are decoded concurrently in bounded batches and written in order only
//! after the whole batch verifies. Parsing/worker failures discard that batch;
//! earlier writes remain. Sink failures may leave partial output. END and exact
//! EOF are required. Errors/cancellation drain outstanding work before completion.
//! Rvalue sources and output iterator adapters are owned. Borrowed sources,
//! sinks, streams, output storage, and scheduler contexts must outlive completion.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, class Destination>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
    && (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
    || std::output_iterator<std::remove_cvref_t<Destination>, std::byte>
    || requires(Destination &sink, std::span<std::byte const> bytes) { { sink.write(bytes) } -> std::same_as<void>; })
[[nodiscard]] auto decompress_parallel_on(Scheduler scheduler,
  R &&source,
  Destination &&destination,
  parallel_decode_options options = {})
{
  auto source_view = std::views::all(std::forward<R>(source));
  auto make_sender = [scheduler = std::move(scheduler), source = std::move(source_view), options](
                       auto &sink) mutable -> auto {
    return stdexec::then(::dacoexec::detail::huffman::decode_parallel_on(std::move(scheduler),
                           std::move(source),
                           sink,
                           { .max_frame_size_ = options.max_frame_size_,
                             .max_output_bytes_ = options.max_output_bytes_,
                             .max_in_flight_ = options.max_in_flight_ }),
      [](auto statistics) -> stream_statistics {
        return { statistics.input_bytes_, statistics.output_bytes_, statistics.frames_ };
      });
  };
  if constexpr (std::derived_from<std::remove_reference_t<Destination>, std::ostream>) {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel streams must be lvalues");
    return stdexec::let_value(stdexec::just(::dacoexec::detail::io::ostream_sink(destination)), std::move(make_sender));
  } else if constexpr (std::output_iterator<std::remove_cvref_t<Destination>, std::byte>) {
    return stdexec::let_value(
      stdexec::just(::dacoexec::detail::io::iterator_sink(std::forward<Destination>(destination))),
      std::move(make_sender));
  } else {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel sinks must be lvalues");
    return std::invoke(std::move(make_sender), destination);
  }
}
}// namespace dacoexec::huffman

namespace dacoexec::ans {
struct parallel_options
{
  std::size_t block_size_ = ::dacoexec::detail::ans::default_block_size;
  std::size_t max_in_flight_ = ::dacoexec::detail::execution::default_max_in_flight;
  entropy_coder coder_ = entropy_coder::rans;
  unsigned table_log_ = ::dacoexec::detail::ans::tans::default_table_log;
};

//! Returns a lazy single-use sender completing with public stream_statistics.
//! Lvalue sources are borrowed; rvalue sources are owned. Borrowed sources, sinks,
//! ostream objects, and scheduler contexts must outlive completion.
//! Writes are ordered; failures and cancellation drain in-flight work. Earlier writes remain.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, class Destination>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
    && (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
    || std::output_iterator<std::remove_cvref_t<Destination>, std::byte>
    || requires(Destination &sink, std::span<std::byte const> bytes) { { sink.write(bytes) } -> std::same_as<void>; })
[[nodiscard]] auto
  compress_parallel_on(Scheduler scheduler, R &&source, Destination &&destination, parallel_options options = {})
{
  auto source_view = std::views::all(std::forward<R>(source));
  auto make_sender = [scheduler = std::move(scheduler), source = std::move(source_view), options](
                       auto &sink) mutable -> auto {
    return stdexec::then(::dacoexec::detail::ans::encode_parallel_on(std::move(scheduler),
                           std::move(source),
                           sink,
                           { .block_size_ = options.block_size_,
                             .max_in_flight_ = options.max_in_flight_,
                             .coder_ = options.coder_,
                             .table_log_ = options.table_log_ }),
      [](auto statistics) -> stream_statistics {
        return { statistics.input_bytes_, statistics.output_bytes_, statistics.frames_ };
      });
  };
  if constexpr (std::derived_from<std::remove_reference_t<Destination>, std::ostream>) {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel streams must be lvalues");
    return stdexec::let_value(stdexec::just(::dacoexec::detail::io::ostream_sink(destination)), std::move(make_sender));
  } else if constexpr (std::output_iterator<std::remove_cvref_t<Destination>, std::byte>) {
    return stdexec::let_value(
      stdexec::just(::dacoexec::detail::io::iterator_sink(std::forward<Destination>(destination))),
      std::move(make_sender));
  } else {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel sinks must be lvalues");
    return std::invoke(std::move(make_sender), destination);
  }
}

//! Limits checked before decoded output allocation; positive decode batch capacity.
struct parallel_decode_options
{
  std::size_t max_frame_size_ = ::dacoexec::detail::ans::max_frame_size;
  std::uint64_t max_output_bytes_ = std::numeric_limits<std::uint64_t>::max();
  std::size_t max_in_flight_ = ::dacoexec::detail::execution::default_max_in_flight;
};

//! Lazy single-use verified decompression returning public stream_statistics.
//! Frames are decoded concurrently in bounded batches and written in order only
//! after the whole batch verifies. Parsing/worker failures discard that batch;
//! earlier writes remain. Sink failures may leave partial output. END and exact
//! EOF are required. Errors/cancellation drain outstanding work before completion.
//! Rvalue sources and output iterator adapters are owned. Borrowed sources,
//! sinks, streams, output storage, and scheduler contexts must outlive completion.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, class Destination>
  requires ::dacoexec::detail::byte_input_range<std::views::all_t<R>>
    && (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
    || std::output_iterator<std::remove_cvref_t<Destination>, std::byte>
    || requires(Destination &sink, std::span<std::byte const> bytes) { { sink.write(bytes) } -> std::same_as<void>; })
[[nodiscard]] auto decompress_parallel_on(Scheduler scheduler,
  R &&source,
  Destination &&destination,
  parallel_decode_options options = {})
{
  auto source_view = std::views::all(std::forward<R>(source));
  auto make_sender = [scheduler = std::move(scheduler), source = std::move(source_view), options](
                       auto &sink) mutable -> auto {
    return stdexec::then(::dacoexec::detail::ans::decode_parallel_on(std::move(scheduler),
                           std::move(source),
                           sink,
                           { .max_frame_size_ = options.max_frame_size_,
                             .max_output_bytes_ = options.max_output_bytes_,
                             .max_in_flight_ = options.max_in_flight_ }),
      [](auto statistics) -> stream_statistics {
        return { statistics.input_bytes_, statistics.output_bytes_, statistics.frames_ };
      });
  };
  if constexpr (std::derived_from<std::remove_reference_t<Destination>, std::ostream>) {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel streams must be lvalues");
    return stdexec::let_value(stdexec::just(::dacoexec::detail::io::ostream_sink(destination)), std::move(make_sender));
  } else if constexpr (std::output_iterator<std::remove_cvref_t<Destination>, std::byte>) {
    return stdexec::let_value(
      stdexec::just(::dacoexec::detail::io::iterator_sink(std::forward<Destination>(destination))),
      std::move(make_sender));
  } else {
    static_assert(std::is_lvalue_reference_v<Destination>, "Parallel sinks must be lvalues");
    return std::invoke(std::move(make_sender), destination);
  }
}
}// namespace dacoexec::ans

#endif// DACOEXEC_EXECUTION_HPP
