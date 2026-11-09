//! \file
//! ANS decoder support.
#ifndef DACOEXEC_ANS_DECODER_HPP
#define DACOEXEC_ANS_DECODER_HPP

#include <dacoexec/ans/encoded_block.hpp>
#include <dacoexec/codec/decoder_base.hpp>

namespace dacoexec::ans {

class decoder : public codec::decoder_base<decoder, encoded_block, decode_error>
{
public:
  decoder() = default;

  [[nodiscard]] static auto output_limit_message() noexcept -> char const *
  { return "ANS output exceeds configured limit"; }

  [[nodiscard]] static auto raw_size_message() noexcept -> char const * { return "Invalid ANS RAW metadata"; }

  [[nodiscard]] static auto is_raw(encoded_block const &block) noexcept -> bool
  { return block.encoding_ == block_encoding::raw; }

  static auto validate(encoded_block const &block) -> void
  {
    if (is_raw(block)) {
      if (block.model_) { throw decode_error("Invalid ANS RAW metadata"); }
    } else if (block.encoding_ != block_encoding::rans || !block.model_) {
      throw decode_error("Invalid ANS block metadata");
    }
  }

  [[nodiscard]] static auto decode_compressed(encoded_block const &block, std::size_t limit) -> std::vector<std::byte>
  {
    if (!block.model_) { throw decode_error("Invalid ANS block metadata"); }
    return rans::decode_block(block.payload_, *block.model_, static_cast<std::size_t>(block.uncompressed_size_), limit);
  }
};

}// namespace dacoexec::ans
#endif// DACOEXEC_ANS_DECODER_HPP
