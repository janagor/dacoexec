//! \file
//! Shared owning block encoding workflow with metadata-aware RAW fallback.
#ifndef DACOEXEC_CODEC_ENCODER_BASE_HPP
#define DACOEXEC_CODEC_ENCODER_BASE_HPP

#include <dacoexec/views/blocks.hpp>
#include <utility>

namespace dacoexec::codec {

// Derived supplies try_encode(span<byte const>) and metadata_size(Block const &).
// Candidates contain codec metadata and payload; the base sets source identity.
// A default-constructed Block must describe RAW with empty codec metadata.
template<typename Derived, typename Block> class encoder_base
{
public:
  [[nodiscard]] auto encode(byte_block block) const -> Block
  {
    auto const size = block.bytes_.size();
    if (size != 0) {
      auto candidate = derived().try_encode(block.bytes_);
      if (candidate) {
        auto const metadata = derived().metadata_size(*candidate);
        // Subtract only after checking metadata to avoid overflowing total cost.
        if (metadata < size && candidate->payload_.size() < size - metadata) {
          candidate->sequence_ = block.sequence_;
          candidate->uncompressed_size_ = size;
          return std::move(*candidate);
        }
      }
    }
    Block raw{};
    raw.sequence_ = block.sequence_;
    raw.uncompressed_size_ = size;
    raw.payload_ = std::move(block.bytes_);
    return raw;
  }

private:
  friend Derived;

  encoder_base() = default;

  [[nodiscard]] auto derived() const noexcept -> Derived const & { return static_cast<Derived const &>(*this); }
};

}// namespace dacoexec::codec
#endif// DACOEXEC_CODEC_ENCODER_BASE_HPP
