//! \file
//! Encoded block requirements shared by codec workflows.
#ifndef DACOEXEC_CODEC_CONCEPTS_HPP
#define DACOEXEC_CODEC_CONCEPTS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dacoexec::detail::codec {

// Codec metadata and encoding tags are deliberately codec-specific. Shared
// workflows require writable identity, decoded size, and owned payload storage.
// Default construction must represent RAW with empty metadata; this semantic
// requirement is enforced by the codec, not by the concept. Copying is optional.
template<typename Block>
concept encoded_block = std::default_initializable<Block> && std::movable<Block> && requires(Block &block) {
  { block.sequence_ } -> std::same_as<std::uint64_t &>;
  { block.uncompressed_size_ } -> std::same_as<std::uint64_t &>;
  { block.payload_ } -> std::same_as<std::vector<std::byte> &>;
};

}// namespace dacoexec::detail::codec
#endif// DACOEXEC_CODEC_CONCEPTS_HPP
