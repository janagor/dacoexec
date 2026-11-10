//! \file
//! Arithmetic block support.

#ifndef DACOEXEC_ARITHMETIC_BLOCK_HPP
#define DACOEXEC_ARITHMETIC_BLOCK_HPP

#include <dacoexec/detail/arithmetic/decoder.hpp>
#include <dacoexec/detail/arithmetic/encoder.hpp>
#include <utility>

namespace dacoexec::detail::arithmetic {

[[nodiscard]] inline auto compress_block(byte_block block) -> encoded_block
{ return encoder{}.encode(std::move(block)); }

[[nodiscard]] inline auto decompress_block(encoded_block block, std::size_t limit) -> byte_block
{ return decoder{}.decode(std::move(block), limit); }

}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_BLOCK_HPP
