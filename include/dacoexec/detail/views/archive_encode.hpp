//! Pull-based archive encoding using the existing frame encoders and serializers.
#ifndef DACOEXEC_VIEWS_ARCHIVE_ENCODE_HPP
#define DACOEXEC_VIEWS_ARCHIVE_ENCODE_HPP
#include <cstddef>
#include <dacoexec/detail/concepts.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/views/archive_bytes.hpp>
#include <dacoexec/detail/views/blocks.hpp>
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
class archive_encoder
{
public:
  using options_type = Format::options_type;

  archive_encoder(V source, options_type options) : blocks_(std::move(source), options.block_size_), options_(options)
  {}

  [[nodiscard]] auto next() -> std::span<std::byte const>
  {
    bytes_.clear();
    io::iterator_sink sink{ std::back_inserter(bytes_) };
    if (!header_written_) {
      Format::write_archive_header(sink);
      header_written_ = true;
    } else if (!end_written_) {
      if (!cursor_) {
        cursor_.emplace(blocks_.begin());
      } else {
        ++*cursor_;
      }
      if (*cursor_ == blocks_.end()) {
        Format::write_end(sink);
        end_written_ = true;
      } else {
        auto frame = Format::encode_frame(std::ranges::iter_move(*cursor_), options_);
        Format::write_frame(sink, frame);
      }
    }
    return bytes_;
  }

private:
  blocks_view<V> blocks_;
  options_type options_;
  std::optional<std::ranges::iterator_t<blocks_view<V>>> cursor_;
  std::vector<std::byte> bytes_;
  bool header_written_ = false;
  bool end_written_ = false;
};

template<class Format> class archive_encode_closure
{
public:
  using options_type = Format::options_type;

  explicit archive_encode_closure(options_type options) : options_(options)
  {
    if (options.block_size_ == 0 || options.block_size_ > Format::max_frame_size) {
      throw std::invalid_argument("Invalid block size");
    }
    Format::validate_options(options);
  }

  template<std::ranges::viewable_range R>
    requires byte_input_range<std::views::all_t<R>>
  [[nodiscard]] auto operator()(R &&source) const
  {
    using producer = archive_encoder<std::views::all_t<R>, Format>;
    return archive_bytes_view<producer>{ producer{ std::views::all(std::forward<R>(source)), options_ } };
  }

  template<std::ranges::viewable_range R>
    requires byte_input_range<std::views::all_t<R>>
  [[nodiscard]] friend auto operator|(R &&source, archive_encode_closure const &closure)
  { return closure(std::forward<R>(source)); }

private:
  options_type options_;
};
}// namespace dacoexec::detail::views
#endif
