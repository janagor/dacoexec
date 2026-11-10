//! Shared frame verification before publishing decoded bytes.
#ifndef DACOEXEC_CODEC_VERIFIED_FRAME_HPP
#define DACOEXEC_CODEC_VERIFIED_FRAME_HPP
#include <cstdint>
#include <dacoexec/detail/support/crc32.hpp>
#include <dacoexec/error.hpp>
#include <utility>

namespace dacoexec::detail::codec {
template<class Format>
[[nodiscard]] auto decode_verified_frame(typename Format::frame_type &frame,
  typename Format::decode_options_type const &options,
  std::uint64_t accepted_bytes)
{
  if (frame.header_.uncompressed_size_ > options.max_output_bytes_ - accepted_bytes) {
    throw ::dacoexec::exception(
      ::dacoexec::errc::output_limit_exceeded, "Decompressed output exceeds configured limit");
  }
  auto decoded = Format::decode_block(std::move(frame.block_), options.max_frame_size_);
  if (support::crc32(decoded.bytes_) != frame.header_.crc32_) {
    throw ::dacoexec::exception(::dacoexec::errc::checksum_mismatch, Format::checksum_message());
  }
  return decoded;
}
}// namespace dacoexec::detail::codec
#endif
