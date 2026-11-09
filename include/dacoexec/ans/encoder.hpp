//! \file
//! ANS encoder support.
#ifndef DACOEXEC_ANS_ENCODER_HPP
#define DACOEXEC_ANS_ENCODER_HPP

#include <dacoexec/ans/encoded_block.hpp>
#include <dacoexec/codec/encoder_base.hpp>
#include <span>

namespace dacoexec::ans {

class encoder : public codec::encoder_base<encoder, encoded_block>
{
public:
  encoder() = default;

  [[nodiscard]] static auto try_encode(std::span<std::byte const> input) -> std::optional<encoded_block>
  {
    if (input.size() <= min_model_wire_size) { return std::nullopt; }
    encoded_block result;
    auto const model = make_model(count_frequencies(input));
    result.payload_ = rans::encode_block(input, model);
    result.encoding_ = block_encoding::rans;
    result.model_ = model;
    return result;
  }

  [[nodiscard]] static auto metadata_size(encoded_block const &block) -> std::size_t
  { return detail::model_metadata_size(block); }
};

}// namespace dacoexec::ans
#endif// DACOEXEC_ANS_ENCODER_HPP
