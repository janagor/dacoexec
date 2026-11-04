#include <dacoexec/huffman/block.hpp>

static_assert(std::movable<dacoexec::huffman::encoded_block>);
static_assert(!std::copyable<dacoexec::huffman::encoded_block>);

[[nodiscard]] auto installed_block_round_trip() -> bool
{
  constexpr std::size_t block_size = 4096;
  constexpr std::uint64_t sequence = 42;
  std::vector<std::byte> const bytes(block_size, std::byte{ 'A' });
  auto encoded = dacoexec::huffman::compress_block({ sequence, bytes });
  if (encoded.encoding_ != dacoexec::huffman::block_encoding::canonical_huffman) { return false; }
  auto decoded = dacoexec::huffman::decompress_block(std::move(encoded), block_size);
  return decoded.sequence_ == sequence && decoded.bytes_ == bytes;
}
