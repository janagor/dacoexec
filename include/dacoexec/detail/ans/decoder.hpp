//! \file
//! ANS decoder support.
#ifndef DACOEXEC_ANS_DECODER_HPP
#define DACOEXEC_ANS_DECODER_HPP

#include <dacoexec/detail/ans/encoded_block.hpp>
#include <dacoexec/detail/ans/tans/codec.hpp>
#include <dacoexec/detail/codec/decoder_base.hpp>
#include <dacoexec/error.hpp>

namespace dacoexec::detail::ans {

class decoder : public codec::decoder_base<decoder, encoded_block>
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
      if (block.model_) { throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid ANS RAW metadata"); }
    } else if ((block.encoding_ != block_encoding::rans && block.encoding_ != block_encoding::tans) || !block.model_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid ANS block metadata");
    }
  }

  [[nodiscard]] static auto decode_compressed(encoded_block const &block, std::size_t limit) -> std::vector<std::byte>
  {
    if (!block.model_) { throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid ANS block metadata"); }
    if (block.encoding_ == block_encoding::tans) {
      return tans::decode_block(
        block.payload_, *block.model_, static_cast<std::size_t>(block.uncompressed_size_), limit);
    }
    return rans::decode_block(block.payload_, *block.model_, static_cast<std::size_t>(block.uncompressed_size_), limit);
  }
};

}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_DECODER_HPP
