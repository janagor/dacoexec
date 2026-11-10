#include "error_policy.hpp"
#include <dacoexec/detail/huffman/block.hpp>
#include <dacoexec/error.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>
#include <vector>

namespace {
namespace huffman = dacoexec::detail::huffman;
constexpr std::size_t input_limit = 65536;
constexpr std::size_t output_limit = input_limit;

// Harness envelope, not a wire format: kind, padding, uint64 LE output size,
// 256 lengths, then payload. Missing fields are zero-filled deliberately.
auto malformed(std::span<std::uint8_t const> input) -> void
{
  auto take = [&input]() -> std::uint8_t {
    if (input.empty()) { return 0; }
    auto const value = input.front();
    input = input.subspan(1);
    return value;
  };
  huffman::encoded_block block;
  block.encoding_ = static_cast<huffman::block_encoding>(take());
  block.padding_bits_ = take();
  constexpr auto byte_bits = dacoexec::detail::support::bits_per_byte;
  for (std::size_t index = 0; index < sizeof(block.uncompressed_size_); ++index) {
    block.uncompressed_size_ |= static_cast<std::uint64_t>(take()) << (index * byte_bits);
  }
  for (auto &length : block.lengths_) { length = take(); }
  for (auto value : input) { block.payload_.push_back(static_cast<std::byte>(value)); }
  auto const expected_size = block.uncompressed_size_;
  try {
    auto const decoded = huffman::decompress_block(std::move(block), output_limit);
    if (decoded.bytes_.size() != expected_size) { std::abort(); }
  } catch (dacoexec::exception const &error) {
    if (!dacoexec::fuzz::expected_huffman_block_error(error.code())) { throw; }
  }
}

}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const input = std::span(data, size).first(std::min(size, input_limit));
  malformed(input);
  return 0;
}
