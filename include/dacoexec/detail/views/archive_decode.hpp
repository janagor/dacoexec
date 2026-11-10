//! Pull-based decoding: a complete frame is verified before exposing any byte.
#ifndef DACOEXEC_VIEWS_ARCHIVE_DECODE_HPP
#define DACOEXEC_VIEWS_ARCHIVE_DECODE_HPP
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/codec/stream_statistics.hpp>
#include <dacoexec/detail/codec/verified_frame.hpp>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/views/archive_bytes.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dacoexec::detail::views {
template<std::ranges::view V, class Format>
  requires byte_input_range<V>
class archive_decoder
{
  using frames_type = decltype(Format::frames(std::declval<V>(), std::size_t{}));

public:
  using options_type = Format::decode_options_type;

  archive_decoder(V source, options_type options)
    : frames_(Format::frames(std::move(source), options.max_frame_size_)), options_(options)
  {}

  [[nodiscard]] auto next() -> std::span<std::byte const>
  {
    bytes_.clear();
    if (!cursor_) {
      cursor_.emplace(frames_.begin());
    } else {
      ++*cursor_;
    }
    if (*cursor_ == frames_.end()) { return {}; }
    auto frame = std::ranges::iter_move(*cursor_);
    auto decoded = codec::decode_verified_frame<Format>(frame, options_, accepted_bytes_);
    codec::checked_add(accepted_bytes_, frame.header_.uncompressed_size_);
    bytes_ = std::move(decoded.bytes_);
    return bytes_;
  }

private:
  frames_type frames_;
  options_type options_;
  std::optional<std::ranges::iterator_t<frames_type>> cursor_;
  std::vector<std::byte> bytes_;
  std::uint64_t accepted_bytes_ = 0;
};

template<class Format> class archive_decode_closure
{
public:
  using options_type = Format::decode_options_type;

  explicit archive_decode_closure(options_type options) : options_(options)
  {
    if (options.max_frame_size_ == 0 || options.max_frame_size_ > Format::max_frame_size) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid frame size limit");
    }
  }

  template<std::ranges::viewable_range R>
    requires byte_input_range<std::views::all_t<R>>
  [[nodiscard]] auto operator()(R &&source) const
  {
    using producer = archive_decoder<std::views::all_t<R>, Format>;
    return archive_bytes_view<producer>{ producer{ std::views::all(std::forward<R>(source)), options_ } };
  }

  template<std::ranges::viewable_range R>
    requires byte_input_range<std::views::all_t<R>>
  [[nodiscard]] friend auto operator|(R &&source, archive_decode_closure const &closure)
  { return closure(std::forward<R>(source)); }

private:
  options_type options_;
};
}// namespace dacoexec::detail::views
#endif
