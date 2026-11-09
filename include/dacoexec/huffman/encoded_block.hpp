//! \file
//! Owning Huffman block representation independent of framing.
#ifndef DACOEXEC_HUFFMAN_ENCODED_BLOCK_HPP
#define DACOEXEC_HUFFMAN_ENCODED_BLOCK_HPP

#include <cstddef>
#include <cstdint>
#include <dacoexec/huffman/canonical.hpp>
#include <vector>

namespace dacoexec::huffman {

/**
 * Payload representation: original bytes or canonical Huffman bits.
 */
enum class block_encoding : std::uint8_t { raw, canonical_huffman };

/**
 * Move-only owning block model, independent of any serialized format.
 * RAW has zero lengths/padding and payload size equal to uncompressed_size_.
 * Huffman represents nonempty input, with canonical lengths and zero-filled
 * low padding bits (fewer than one byte). sequence_ survives both operations.
 */
struct encoded_block
{
  //! Source block position, preserved across compression and decompression.
  std::uint64_t sequence_ = 0;
  //! Selects payload interpretation.
  block_encoding encoding_ = block_encoding::raw;
  //! Exact decoded byte count.
  std::uint64_t uncompressed_size_ = 0;
  //! Owned RAW bytes or MSB-first packed Huffman data.
  std::vector<std::byte> payload_;
  //! Per-symbol canonical widths; all zero for RAW.
  code_length_table lengths_{};
  //! Unused zero low bits in the last Huffman byte; zero for RAW.
  std::uint8_t padding_bits_ = 0;

  encoded_block() = default;
  encoded_block(encoded_block &&) noexcept = default;
  auto operator=(encoded_block &&) noexcept -> encoded_block & = default;
  encoded_block(encoded_block const &) = delete;
  auto operator=(encoded_block const &) -> encoded_block & = delete;
  ~encoded_block() = default;
};

}// namespace dacoexec::huffman
#endif// DACOEXEC_HUFFMAN_ENCODED_BLOCK_HPP
