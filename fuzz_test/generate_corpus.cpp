// Optional developer utility: regenerate valid seeds through the public encoder.
#include "../benchmarks/fixtures.hpp"

#include <dacoexec/huffman/stream.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
auto save(std::filesystem::path const &path, std::vector<std::byte> const &bytes) -> void
{
  std::ofstream output(path, std::ios::binary);
  for (auto byte : bytes) { output.put(static_cast<char>(std::to_integer<unsigned char>(byte))); }
  if (!output) { throw std::runtime_error("Cannot write corpus seed"); }
}
}// namespace

auto main(int argc, char **argv) -> int
{
  if (argc != 2) { return 1; }
  namespace huffman = dacoexec::huffman;
  auto const root = std::filesystem::path(argv[1]);
  for (auto directory : { "huffman_decoder", "frame_parser", "round_trip" }) {
    std::filesystem::create_directories(root / directory);
  }
  constexpr std::size_t seed_size = 4097;
  for (auto kind : fixtures::datasets) {
    auto const name = std::string(fixtures::names.at(static_cast<std::size_t>(kind)));
    auto input = fixtures::make(seed_size, kind);
    auto block = huffman::compress_block({ 0, input });
    std::vector<std::byte> envelope{ static_cast<std::byte>(block.encoding_),
      static_cast<std::byte>(block.padding_bits_) };
    for (std::size_t index = 0; index < sizeof(block.uncompressed_size_); ++index) {
      auto const shift = index * dacoexec::detail::bits_per_byte;
      envelope.push_back(static_cast<std::byte>(block.uncompressed_size_ >> shift));
    }
    for (auto length : block.lengths_) { envelope.push_back(static_cast<std::byte>(length)); }
    envelope.insert(envelope.end(), block.payload_.begin(), block.payload_.end());
    save(root / "huffman_decoder" / name, envelope);
    save(root / "round_trip" / name, input);
    constexpr std::array frame_block_sizes{ std::size_t{ 256 }, std::size_t{ 1024 } };
    for (auto block_size : frame_block_sizes) {
      std::vector<std::byte> archive;
      dacoexec::io::iterator_sink sink{ std::back_inserter(archive) };
      static_cast<void>(huffman::compress_stream(input, sink, { .block_size_ = block_size }));
      save(root / "frame_parser" / (name + "-blocks-" + std::to_string(block_size)), archive);
    }
  }
  return 0;
}
