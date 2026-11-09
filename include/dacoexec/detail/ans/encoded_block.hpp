//! \file
//! ANS encoded block support.
#ifndef DACOEXEC_ANS_ENCODED_BLOCK_HPP
#define DACOEXEC_ANS_ENCODED_BLOCK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/ans/model_wire.hpp>
#include <dacoexec/detail/ans/rans.hpp>
#include <optional>
#include <vector>

namespace dacoexec::detail::ans {

enum class block_encoding : std::uint8_t { raw, rans };

struct encoded_block
{
  std::uint64_t sequence_ = 0;
  block_encoding encoding_ = block_encoding::raw;
  std::uint64_t uncompressed_size_ = 0;
  std::vector<std::byte> payload_;
  std::optional<frequency_model<alphabet_size>> model_;
};

[[nodiscard]] inline auto model_metadata_size(encoded_block const &block) -> std::size_t
{
  if (block.encoding_ == block_encoding::raw) { return 0; }
  if (!block.model_) { throw decode_error("Missing ANS model"); }
  return model_wire_size(*block.model_);
}

}// namespace dacoexec::detail::ans
#endif// DACOEXEC_ANS_ENCODED_BLOCK_HPP
