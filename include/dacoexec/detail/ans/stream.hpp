//! Incremental archive compression and decompression.
#ifndef DACOEXEC_ANS_STREAM_HPP
#define DACOEXEC_ANS_STREAM_HPP
#include <dacoexec/detail/ans/archive_format.hpp>
#include <dacoexec/detail/codec/stream.hpp>

namespace dacoexec::detail::ans {
//! Incremental archive writer. Exceptions propagate; prior sink writes remain.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto compress_stream(R &&source, S &sink, stream_options options = {}) -> stream_statistics
{ return codec::compress_stream<stream_format>(std::forward<R>(source), sink, options); }

//! Verifies each complete frame before writing; earlier frames remain on error.
//! Size limits are checked before output allocation. END and exact EOF are required.
template<std::ranges::viewable_range R, io::byte_output_sink S>
  requires byte_input_range<std::views::all_t<R>>
[[nodiscard]] auto decompress_stream(R &&source, S &sink, decode_options options = {}) -> stream_statistics
{ return codec::decompress_stream<stream_format>(std::forward<R>(source), sink, options); }
}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_STREAM_HPP
