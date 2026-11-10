//! \file
//! Archive, byte/bit, and integer range adaptors. Concrete view and iterator types are unspecified.
#ifndef DACOEXEC_VIEWS_HPP
#define DACOEXEC_VIEWS_HPP
#include <cstdint>
#include <dacoexec/ans.hpp>
#include <dacoexec/detail/views/archive_decode.hpp>
#include <dacoexec/detail/views/archive_encode.hpp>
#include <dacoexec/detail/views/bits.hpp>
#include <dacoexec/detail/views/integer_decode.hpp>
#include <dacoexec/detail/views/integer_encode.hpp>
#include <dacoexec/detail/views/positive.hpp>
#include <dacoexec/detail/views/zigzag.hpp>
#include <dacoexec/huffman.hpp>
#include <limits>

namespace dacoexec::views {
//! Lazily yields MSB-first bool bits from byte input.
[[nodiscard]] constexpr auto bits() { return ::dacoexec::detail::views::bits(); }

//! Maximum unary prefix per decoded integer, excluding its terminating zero.
//! Exceeding this bound reports errc::codeword_limit_exceeded.
struct decode_limits
{
  std::uint64_t max_unary_bits_ = std::numeric_limits<std::uint64_t>::max();
};

//! Archive adaptors yield std::byte values in a move-only single-pass view.
//! Construction validates options without iterating the source. begin() returns
//! a cursor without processing input or output. Iterator copies
//! share a pass; repeated begin() resumes it. Moving a view preserves iterators;
//! destroying its state invalidates them. Lvalue containers are borrowed and
//! rvalue containers owned according to views::all. Borrowed storage must survive
//! iteration. Default and moved-from views are empty; const iteration is unsupported.
//! Any processing exception ends the pass permanently. Destruction never drains it.
//! Additional working memory is bounded by frame size and codec workspace,
//! excluding source-owned storage. Initially and at chunk boundaries, loading is
//! deferred until dereference, increment, or end comparison; these operations can
//! throw. std::views::take(0) performs no producer work, even on invalid archives.
namespace encode {
  //! Maps signed integers to uint64_t, keeping values near zero small.
  //! Mapping views use transform semantics: values are computed on dereference
  //! without caching. Signed input types up to 64 bits are accepted.
  [[nodiscard]] constexpr auto zigzag() { return ::dacoexec::detail::views::encode::zigzag(); }

  //! Adds one to unsigned integers for Gamma/Delta coding. UINT64_MAX throws
  //! errc::numeric_overflow on dereference. Yields uint64_t.
  [[nodiscard]] constexpr auto positive() { return ::dacoexec::detail::views::encode::positive(); }

  //! Produces a complete DHF1 archive, buffering one frame plus codec workspace.
  //! First demand stages only the archive header. Exhaust the view to produce END.
  [[nodiscard]] inline auto huffman(::dacoexec::huffman::stream_options options = {})
  {
    return ::dacoexec::detail::views::archive_encode_closure<::dacoexec::detail::huffman::stream_format>{
      { .block_size_ = options.block_size_ }
    };
  }

  //! Produces a complete DAN1 archive, using rANS by default or the selected coder.
  [[nodiscard]] inline auto ans(::dacoexec::ans::stream_options options = {})
  {
    return ::dacoexec::detail::views::archive_encode_closure<::dacoexec::detail::ans::stream_format>{
      { .block_size_ = options.block_size_, .coder_ = options.coder_, .table_log_ = options.table_log_ }
    };
  }

  [[nodiscard]] constexpr auto gamma() { return ::dacoexec::detail::views::encode::gamma(); }

  [[nodiscard]] constexpr auto delta() { return ::dacoexec::detail::views::encode::delta(); }

  [[nodiscard]] constexpr auto golomb(std::uint32_t divisor)
  { return ::dacoexec::detail::views::encode::golomb(divisor); }

  [[nodiscard]] constexpr auto rice(unsigned shift) { return ::dacoexec::detail::views::encode::rice(shift); }
}// namespace encode

namespace decode {
  //! Reverses ZigZag mapping of unsigned integers, yielding int64_t.
  [[nodiscard]] constexpr auto zigzag() { return ::dacoexec::detail::views::decode::zigzag(); }

  //! Subtracts one from positive unsigned integers, yielding uint64_t.
  //! Zero throws errc::invalid_argument on dereference.
  [[nodiscard]] constexpr auto positive() { return ::dacoexec::detail::views::decode::positive(); }

  //! Verifies each whole frame before yielding bytes. Full archive validation,
  //! including END and trailing-input checks, requires exhausting the view.
  //! Earlier verified bytes remain available to the consumer after later errors.
  [[nodiscard]] inline auto huffman(::dacoexec::huffman::decode_options options = {})
  {
    return ::dacoexec::detail::views::archive_decode_closure<::dacoexec::detail::huffman::stream_format>{
      { .max_frame_size_ = options.max_frame_size_, .max_output_bytes_ = options.max_output_bytes_ }
    };
  }

  //! Decodes either rANS or tANS DAN1 frames with the same verification contract.
  [[nodiscard]] inline auto ans(::dacoexec::ans::decode_options options = {})
  {
    return ::dacoexec::detail::views::archive_decode_closure<::dacoexec::detail::ans::stream_format>{
      { .max_frame_size_ = options.max_frame_size_, .max_output_bytes_ = options.max_output_bytes_ }
    };
  }

  [[nodiscard]] constexpr auto gamma(std::uint64_t count) { return ::dacoexec::detail::views::decode::gamma(count); }

  [[nodiscard]] constexpr auto delta(std::uint64_t count) { return ::dacoexec::detail::views::decode::delta(count); }

  [[nodiscard]] constexpr auto golomb(std::uint32_t divisor, std::uint64_t count, decode_limits limits = {})
  { return ::dacoexec::detail::views::decode::golomb(divisor, count, { .max_unary_bits_ = limits.max_unary_bits_ }); }

  [[nodiscard]] constexpr auto rice(unsigned shift, std::uint64_t count, decode_limits limits = {})
  { return ::dacoexec::detail::views::decode::rice(shift, count, { .max_unary_bits_ = limits.max_unary_bits_ }); }
}// namespace decode

}// namespace dacoexec::views

#endif// DACOEXEC_VIEWS_HPP
