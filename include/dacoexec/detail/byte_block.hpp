//! \file
//! Independently owned uncompressed byte block shared by views and codecs.
#ifndef DACOEXEC_BYTE_BLOCK_HPP
#define DACOEXEC_BYTE_BLOCK_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace dacoexec::detail {

/**
 * One owned slice of a byte stream.
 *
 * sequence_ starts at zero for the first block and increases by one. bytes_ is
 * the block's own storage. The type is move-only so a block is transferred
 * instead of copied. Moved-from blocks keep a valid sequence and a valid,
 * unspecified buffer.
 */
struct byte_block
{
  //! Zero-based block position assigned by blocks_view; preserved by the codec.
  std::uint64_t sequence_ = 0;
  //! Independently owned uncompressed bytes.
  std::vector<std::byte> bytes_;

  byte_block() = default;

  byte_block(std::uint64_t seq, std::vector<std::byte> data) noexcept : sequence_(seq), bytes_(std::move(data)) {}

  byte_block(byte_block &&) noexcept = default;
  auto operator=(byte_block &&) noexcept -> byte_block & = default;

  byte_block(byte_block const &) = delete;
  auto operator=(byte_block const &) -> byte_block & = delete;

  ~byte_block() = default;
};

}// namespace dacoexec::detail
#endif// DACOEXEC_BYTE_BLOCK_HPP
