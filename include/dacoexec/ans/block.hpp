//! \file
//! ANS block support.
#ifndef DACOEXEC_ANS_BLOCK_HPP
#define DACOEXEC_ANS_BLOCK_HPP

#include <dacoexec/ans/decoder.hpp>
#include <dacoexec/ans/encoder.hpp>
#include <utility>

namespace dacoexec::ans {

[[nodiscard]] inline auto compress_block(byte_block block) -> encoded_block
{ return encoder{}.encode(std::move(block)); }

[[nodiscard]] inline auto decompress_block(encoded_block block, std::size_t limit) -> byte_block
{ return decoder{}.decode(std::move(block), limit); }

}// namespace dacoexec::ans
#endif// DACOEXEC_ANS_BLOCK_HPP
