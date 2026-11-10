//! \file
//! Arithmetic encoded block support.
#ifndef DACOEXEC_ARITHMETIC_ENCODED_BLOCK_HPP
#define DACOEXEC_ARITHMETIC_ENCODED_BLOCK_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/static_model.hpp>

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
  //! Metadata for static_range only; future variants define their own metadata.
  std::optional<static_model> model_;
};

[[nodiscard]] inline auto model_metadata_size(encoded_block const &block) -> std::size_t
{
  if (block.encoding_ == block_encoding::raw) { return 0; }
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
