//! \file
//! Optional lazy parallel archive compression; requires dacoexec::execution and stdexec.
#ifndef DACOEXEC_EXECUTION_HPP
#define DACOEXEC_EXECUTION_HPP
#include <concepts>
#include <cstddef>
#include <dacoexec/ans.hpp>
#include <dacoexec/detail/ans/execution.hpp>
#include <dacoexec/detail/huffman/execution.hpp>
#include <dacoexec/huffman.hpp>
#include <functional>
#include <iterator>
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
  requires (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
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
}// namespace dacoexec::huffman

namespace dacoexec::ans {
struct parallel_options
{
  std::size_t block_size_ = ::dacoexec::detail::ans::default_block_size;
  std::size_t max_in_flight_ = ::dacoexec::detail::execution::default_max_in_flight;
};

//! Returns a lazy single-use sender completing with public stream_statistics.
//! Lvalue sources are borrowed; rvalue sources are owned. Borrowed sources, sinks,
//! ostream objects, and scheduler contexts must outlive completion.
//! Writes are ordered; failures and cancellation drain in-flight work. Earlier writes remain.
template<stdexec::scheduler Scheduler, std::ranges::viewable_range R, class Destination>
  requires (std::derived_from<std::remove_reference_t<Destination>, std::ostream>
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
}// namespace dacoexec::ans

#endif// DACOEXEC_EXECUTION_HPP
