//! Lazy DAC1 framing using the shared owning frame view.
#ifndef DACOEXEC_VIEWS_ARITHMETIC_FRAMES_HPP
#define DACOEXEC_VIEWS_ARITHMETIC_FRAMES_HPP
#include <dacoexec/detail/codec/frames_view.hpp>
#include <dacoexec/detail/views/arithmetic_frame_parser.hpp>

namespace dacoexec::detail {
template<std::ranges::view V>
  requires byte_input_range<V>
using arithmetic_frames_view = codec::frames_view<V, support::arithmetic_frame_format>;

namespace views {
  using arithmetic_frames_closure = codec::frames_closure<support::arithmetic_frame_format>;

  [[nodiscard]] constexpr auto arithmetic_frames(std::size_t frame_limit = arithmetic::max_frame_size)
    -> arithmetic_frames_closure
  { return arithmetic_frames_closure{ frame_limit }; }
}// namespace views
}// namespace dacoexec::detail
#endif
