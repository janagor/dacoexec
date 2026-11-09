#ifndef DACOEXEC_DETAIL_FACADE_SINK_HPP
#define DACOEXEC_DETAIL_FACADE_SINK_HPP
#include <concepts>
#include <cstddef>
#include <dacoexec/detail/io/output_sink.hpp>
#include <functional>
#include <iterator>
#include <ostream>
#include <span>
#include <type_traits>
#include <utility>

namespace dacoexec::detail {
template<class D, class F> auto with_byte_sink(D &&destination, F &&function)
{
  if constexpr (std::derived_from<std::remove_reference_t<D>, std::ostream>) {
    io::ostream_sink sink(destination);
    return std::invoke(std::forward<F>(function), sink);
  } else if constexpr (std::output_iterator<std::remove_cvref_t<D>, std::byte>) {
    io::iterator_sink sink(std::forward<D>(destination));
    return std::invoke(std::forward<F>(function), sink);
  } else {
    return std::invoke(std::forward<F>(function), destination);
  }
}
}// namespace dacoexec::detail

#endif// DACOEXEC_DETAIL_FACADE_SINK_HPP
