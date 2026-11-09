//! \file
//! ANS encoder support.
#ifndef DACOEXEC_ANS_ENCODER_HPP
#define DACOEXEC_ANS_ENCODER_HPP

#include <dacoexec/detail/ans/coder.hpp>
#include <dacoexec/detail/ans/encoded_block.hpp>
#include <dacoexec/detail/ans/tans/codec.hpp>
#include <dacoexec/detail/codec/encoder_base.hpp>
#include <span>

namespace dacoexec::detail::ans {

class encoder : public codec::encoder_base<encoder, encoded_block>
{
public:
  encoder() = default;

  explicit encoder(entropy_coder coder, unsigned precision = tans::default_table_log)
    : coder_(coder), precision_(precision)
  { validate_coder(coder_, precision_); }

  [[nodiscard]] auto try_encode(std::span<std::byte const> input) const -> std::optional<encoded_block>
  {
    if (input.size() <= ::dacoexec::detail::ans::min_model_wire_size) { return std::nullopt; }
    encoded_block result;
    auto const model =
      make_model(count_frequencies(input), coder_ == entropy_coder::tans ? precision_ : default_precision_bits);
    result.payload_ =
      coder_ == entropy_coder::tans ? tans::encode_block(input, model) : rans::encode_block(input, model);
    result.encoding_ = coder_ == entropy_coder::tans ? block_encoding::tans : block_encoding::rans;
    result.model_ = model;
    return result;
  }

  [[nodiscard]] static auto metadata_size(encoded_block const &block) -> std::size_t
  { return ::dacoexec::detail::ans::model_metadata_size(block); }

private:
  entropy_coder coder_ = entropy_coder::rans;
  unsigned precision_ = tans::default_table_log;
};

}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_ENCODER_HPP
