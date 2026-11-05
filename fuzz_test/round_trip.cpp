#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/stream.hpp>
#include <dacoexec/io/output_sink.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace {
namespace huffman = dacoexec::huffman;
constexpr std::size_t input_limit = 65536;
constexpr std::size_t output_limit = input_limit;
constexpr std::size_t stream_block_size = 1024;

auto round_trip(std::span<std::uint8_t const> input) -> void
{
  std::vector<std::byte> bytes;
  bytes.reserve(input.size());
  for (auto value : input) { bytes.push_back(static_cast<std::byte>(value)); }
  auto first = huffman::compress_block({ 0, bytes });
  auto const second = huffman::compress_block({ 0, bytes });
  if (first.encoding_ != second.encoding_ || first.payload_ != second.payload_ || first.lengths_ != second.lengths_
      || first.padding_bits_ != second.padding_bits_ || first.uncompressed_size_ != second.uncompressed_size_) {
    std::abort();
  }
  auto const decoded = huffman::decompress_block(std::move(first), output_limit);
  if (decoded.bytes_ != bytes) { std::abort(); }
}

auto stream_round_trip(std::span<std::uint8_t const> input) -> void
{
  std::vector<std::byte> archive;
  dacoexec::io::iterator_sink sink{ std::back_inserter(archive) };
  auto const compressed = huffman::compress_stream(input, sink, { .block_size_ = stream_block_size });
  std::vector<std::byte> decoded;
  dacoexec::io::iterator_sink output{ std::back_inserter(decoded) };
  auto const statistics = huffman::decompress_stream(
    archive, output, { .max_frame_size_ = stream_block_size, .max_output_bytes_ = output_limit });
  if (statistics.output_bytes_ != input.size() || compressed.output_bytes_ != archive.size()
      || decoded.size() != input.size()) {
    std::abort();
  }
  if (!std::ranges::equal(
        input, decoded, {}, [](std::uint8_t byte) -> std::byte { return static_cast<std::byte>(byte); })) {
    std::abort();
  }
}
}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const input = std::span(data, size).first(std::min(size, input_limit));
  round_trip(input);
  stream_round_trip(input);
  return 0;
}
