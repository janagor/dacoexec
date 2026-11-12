//! \file
//! Arithmetic encoder support.
#ifndef DACOEXEC_ARITHMETIC_ENCODER_HPP
#define DACOEXEC_ARITHMETIC_ENCODER_HPP

#include <dacoexec/detail/arithmetic/binary/context/codec.hpp>
#include <dacoexec/detail/arithmetic/coder.hpp>
#include <dacoexec/detail/arithmetic/encoded_block.hpp>
#include <dacoexec/detail/arithmetic/range/adaptive/codec.hpp>
#include <dacoexec/detail/arithmetic/range/static/codec.hpp>
#include <dacoexec/detail/codec/encoder_base.hpp>
#include <span>

namespace dacoexec::detail::arithmetic {

class encoder : public codec::encoder_base<encoder, encoded_block>
{
public:
  static constexpr std::size_t minimum_compressed_size = static_model_min_metadata_size + range_flush_bytes;

  encoder() = default;

  explicit encoder(entropy_coder coder) : coder_(coder) { validate_coder(coder); }

  [[nodiscard]] auto try_encode(std::span<std::byte const> input) const -> std::optional<encoded_block>
  {
    if (coder_ == entropy_coder::context_binary) {
      if (input.size() <= binary_min_payload_size) { return std::nullopt; }
      encoded_block result;
      result.payload_ = encode_context_binary_block(input);
      result.encoding_ = block_encoding::context_binary;
      return result;
    }
    auto const minimum = coder_ == entropy_coder::adaptive_range ? range_flush_bytes : minimum_compressed_size;
    if (coder_ == entropy_coder::binary) {
      if (input.size() <= binary_model_metadata_size + binary_min_payload_size) { return std::nullopt; }
      encoded_block result;
      result.binary_model_ = make_binary_model(input);
      result.payload_ = encode_binary_block(input, *result.binary_model_);
      result.encoding_ = block_encoding::binary;
      return result;
    }
    if (input.size() <= minimum) { return std::nullopt; }
    encoded_block result;
    if (coder_ == entropy_coder::adaptive_range) {
      result.payload_ = encode_adaptive_block(input);
      result.encoding_ = block_encoding::adaptive_range;
      return result;
    }
    auto const model = make_static_model(input);
    result.payload_ = encode_block(input, model);
    result.encoding_ = block_encoding::static_range;
    result.model_ = model;
    return result;
  }

  [[nodiscard]] static auto metadata_size(encoded_block const &block) -> std::size_t
  { return ::dacoexec::detail::arithmetic::model_metadata_size(block); }

private:
  entropy_coder coder_ = entropy_coder::static_range;
};

}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_ENCODER_HPP
