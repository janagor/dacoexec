//! \file
//! Arithmetic encoded block support.
#ifndef DACOEXEC_ARITHMETIC_ENCODED_BLOCK_HPP
#define DACOEXEC_ARITHMETIC_ENCODED_BLOCK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/binary/codec.hpp>
#include <dacoexec/detail/arithmetic/range/static/model.hpp>

#include <dacoexec/error.hpp>
#include <optional>
#include <vector>

namespace dacoexec::detail::arithmetic {

enum class block_encoding : std::uint8_t {
  raw = 0x00,
  static_range = 0x01,
  adaptive_range = 0x02,
  binary = 0x03,
  context_binary = 0x04
};

struct encoded_block
{
  std::uint64_t sequence_ = 0;
  block_encoding encoding_ = block_encoding::raw;
  std::uint64_t uncompressed_size_ = 0;
  std::vector<std::byte> payload_;
  //! Separate fixed models for static range and binary frames.
  std::optional<static_model> model_;
  std::optional<binary_model> binary_model_;
};

[[nodiscard]] inline auto model_metadata_size(encoded_block const &block) -> std::size_t
{
  if (block.encoding_ == block_encoding::raw || block.encoding_ == block_encoding::adaptive_range
      || block.encoding_ == block_encoding::context_binary) {
    return 0;
  }
  if (block.encoding_ == block_encoding::binary) {
    if (!block.binary_model_) {
      throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Missing binary model");
    }
    static_cast<void>(block.binary_model_->total());
    return binary_model_metadata_size;
  }
  if (block.encoding_ != block_encoding::static_range) {
    throw ::dacoexec::exception(::dacoexec::errc::unsupported_coder, "Unsupported arithmetic entropy coder");
  }
  if (!block.model_) { throw ::dacoexec::exception(::dacoexec::errc::invalid_payload, "Missing arithmetic model"); }
  auto const present =
    std::ranges::count_if(block.model_->frequencies(), [](auto frequency) { return frequency != 0; });
  return 3 + 3 * static_cast<std::size_t>(present);
}

}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_ENCODED_BLOCK_HPP
