//! \file
//! ANS block support.

#ifndef DACOEXEC_ANS_BLOCK_HPP
#define DACOEXEC_ANS_BLOCK_HPP

#include <dacoexec/detail/ans/decoder.hpp>
#include <dacoexec/detail/ans/encoder.hpp>
#include <utility>

namespace dacoexec::detail::ans {

[[nodiscard]] inline auto compress_block(byte_block block) -> encoded_block
{ return encoder{}.encode(std::move(block)); }

[[nodiscard]] inline auto decompress_block(encoded_block block, std::size_t limit) -> byte_block
{ return decoder{}.decode(std::move(block), limit); }

}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_BLOCK_HPP
