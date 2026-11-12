//! \file
//! Arithmetic decoder support.
#ifndef DACOEXEC_ARITHMETIC_DECODER_HPP
#define DACOEXEC_ARITHMETIC_DECODER_HPP

#include <dacoexec/detail/arithmetic/binary/codec.hpp>
#include <dacoexec/detail/arithmetic/binary/context/codec.hpp>
#include <dacoexec/detail/arithmetic/encoded_block.hpp>
#include <dacoexec/detail/arithmetic/range/adaptive/codec.hpp>
#include <dacoexec/detail/arithmetic/range/static/codec.hpp>
#include <dacoexec/detail/codec/decoder_base.hpp>
#include <dacoexec/error.hpp>

namespace dacoexec::detail::arithmetic {

class decoder : public codec::decoder_base<decoder, encoded_block>
{
public:
  decoder() = default;

  [[nodiscard]] static auto output_limit_message() noexcept -> char const *
  { return "Arithmetic output exceeds configured limit"; }

  [[nodiscard]] static auto raw_size_message() noexcept -> char const * { return "Invalid arithmetic RAW metadata"; }

  [[nodiscard]] static auto is_raw(encoded_block const &block) noexcept -> bool
  { return block.encoding_ == block_encoding::raw; }

  static auto validate(encoded_block const &block) -> void
  {
    if ((block.encoding_ == block_encoding::binary) != block.binary_model_.has_value()) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid binary block metadata");
    }
    if (block.encoding_ == block_encoding::binary) {
      if (block.model_) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Unexpected static model metadata");
      }
      static_cast<void>(block.binary_model_->total());
      return;
    }
    if (is_raw(block) || block.encoding_ == block_encoding::adaptive_range
        || block.encoding_ == block_encoding::context_binary) {
      if (block.model_) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Unexpected arithmetic model metadata");
      }
    } else if ((block.encoding_ != block_encoding::static_range) || !block.model_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid arithmetic block metadata");
    }
  }

  [[nodiscard]] static auto decode_compressed(encoded_block const &block, std::size_t limit) -> std::vector<std::byte>
  {
    if (block.encoding_ == block_encoding::context_binary) {
      return decode_context_binary_block(block.payload_, static_cast<std::size_t>(block.uncompressed_size_), limit);
    }
    if (block.encoding_ == block_encoding::binary) {
      return decode_binary_block(
        block.payload_, *block.binary_model_, static_cast<std::size_t>(block.uncompressed_size_), limit);
    }
    if (block.encoding_ == block_encoding::adaptive_range) {
      return decode_adaptive_block(block.payload_, static_cast<std::size_t>(block.uncompressed_size_), limit);
    }
    if (!block.model_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Invalid arithmetic block metadata");
    }
    return arithmetic::decode_block(
      block.payload_, *block.model_, static_cast<std::size_t>(block.uncompressed_size_), limit);
  }
};

}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_DECODER_HPP
