//! Lazy DAN1 framing using the shared owning frame view.
#ifndef DACOEXEC_VIEWS_ANS_FRAMES_HPP
#define DACOEXEC_VIEWS_ANS_FRAMES_HPP
#include <dacoexec/detail/codec/frames_view.hpp>
#include <dacoexec/detail/views/ans_frame_parser.hpp>

namespace dacoexec::detail {
template<std::ranges::view V>
  requires byte_input_range<V>
using ans_frames_view = codec::frames_view<V, support::ans_frame_format>;

namespace views {
  using ans_frames_closure = codec::frames_closure<support::ans_frame_format>;

  [[nodiscard]] constexpr auto ans_frames(std::size_t frame_limit = ans::max_frame_size) -> ans_frames_closure
  { return ans_frames_closure{ frame_limit }; }
}// namespace views
}// namespace dacoexec::detail
#endif
