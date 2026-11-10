//! \file
//! Shared bounded block decoding with codec-specific metadata validation.
#ifndef DACOEXEC_CODEC_DECODER_BASE_HPP
#define DACOEXEC_CODEC_DECODER_BASE_HPP

#include <cstddef>
#include <dacoexec/detail/views/blocks.hpp>
#include <dacoexec/error.hpp>
#include <utility>

namespace dacoexec::detail::codec {

// Derived validates metadata before allocation, identifies RAW, and decodes
// compressed payloads. Extra arguments are available to validation and decoding.
// Message hooks preserve codec-specific limit and RAW diagnostics.
template<typename Derived, typename Block> class decoder_base
{
public:
  template<typename... Args>
  [[nodiscard]] auto decode(Block block, std::size_t limit, Args &&...args) const -> byte_block
  {
    if (block.uncompressed_size_ > limit) {
      throw exception(errc::output_limit_exceeded, derived().output_limit_message());
    }
    derived().validate(block, args...);
    if (derived().is_raw(block)) {
      if (block.payload_.size() != block.uncompressed_size_) {
        throw exception(errc::invalid_payload, derived().raw_size_message());
      }
      return { block.sequence_, std::move(block.payload_) };
    }
    auto output = derived().decode_compressed(block, limit, std::forward<Args>(args)...);
    if (output.size() != block.uncompressed_size_) { throw exception(errc::invalid_payload, "Decoded size mismatch"); }
    return { block.sequence_, std::move(output) };
  }

private:
  friend Derived;

  decoder_base() = default;

  [[nodiscard]] auto derived() const noexcept -> Derived const & { return static_cast<Derived const &>(*this); }
};

}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_DECODER_BASE_HPP
