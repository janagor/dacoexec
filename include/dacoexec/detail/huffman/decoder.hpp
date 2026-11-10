//! \file
//! Checked Huffman block decoder with explicit or inferred padding.
#ifndef DACOEXEC_HUFFMAN_DECODER_HPP
#define DACOEXEC_HUFFMAN_DECODER_HPP

#include <algorithm>
#include <dacoexec/detail/codec/decoder_base.hpp>
#include <dacoexec/detail/huffman/block_codec.hpp>
#include <dacoexec/detail/huffman/decode_view.hpp>
#include <dacoexec/detail/huffman/encoded_block.hpp>
#include <dacoexec/error.hpp>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace dacoexec::detail::huffman {


class decoder : public codec::decoder_base<decoder, encoded_block>
{
public:
  decoder() = default;

  [[nodiscard]] static auto output_limit_message() noexcept -> char const *
  { return "Huffman output exceeds configured limit"; }

  [[nodiscard]] static auto raw_size_message() noexcept -> char const * { return "Inconsistent RAW block metadata"; }

  [[nodiscard]] static auto is_raw(encoded_block const &block) noexcept -> bool
  { return block.encoding_ == block_encoding::raw; }

  static auto validate(encoded_block const &block, bool infer_padding = false) -> void
  {
    ::dacoexec::detail::huffman::validate_block(block);
    if (infer_padding && block.padding_bits_ != 0) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Inferred padding requires a zero padding count");
    }
  }

  [[nodiscard]] static auto decode_compressed(encoded_block const &block,
    std::size_t /*limit*/,
    bool infer_padding = false) -> std::vector<std::byte>
  {
    auto const book = ::dacoexec::detail::huffman::make_decode_codebook(block.lengths_);
    auto const valid_bits = ::dacoexec::detail::huffman::meaningful_bit_count(block);
    return ::dacoexec::detail::huffman::decode_payload(block, book, valid_bits, infer_padding);
  }
};

}// namespace dacoexec::detail::huffman
#endif// DACOEXEC_HUFFMAN_DECODER_HPP
