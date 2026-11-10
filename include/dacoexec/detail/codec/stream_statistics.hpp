//! \file
//! Shared stream accounting for framed codecs.
#ifndef DACOEXEC_CODEC_STREAM_STATISTICS_HPP
#define DACOEXEC_CODEC_STREAM_STATISTICS_HPP

#include <cstdint>
#include <dacoexec/error.hpp>
#include <limits>
#include <stdexcept>

namespace dacoexec::detail::codec {

/**
 * Byte and data-frame counts returned only after successful completion.
 *
 * Archive byte counts include archive and frame headers, codec metadata, and
 * encoded payloads. Plain byte counts include only uncompressed data.
 * No statistics are returned on failure.
 */
struct stream_statistics
{
  //! Plain bytes for compression; complete archive bytes for decompression.
  std::uint64_t input_bytes_ = 0;
  //! Complete archive bytes for compression; plain bytes for decompression.
  std::uint64_t output_bytes_ = 0;
  //! Number of data frames; excludes END and the archive header.
  std::uint64_t frames_ = 0;
};

inline auto checked_add(std::uint64_t &total, std::uint64_t amount) -> void
{
  if (amount > std::numeric_limits<std::uint64_t>::max() - total) {
    throw ::dacoexec::exception(::dacoexec::errc::numeric_overflow, "Stream statistics overflow");
  }
  total += amount;
}

}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_STREAM_STATISTICS_HPP
