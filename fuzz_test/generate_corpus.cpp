// Optional developer utility: regenerate valid seeds through the public encoder.
#include "../benchmarks/fixtures.hpp"

#include <dacoexec/ans.hpp>
#include <dacoexec/detail/arithmetic/archive_format.hpp>
#include <dacoexec/detail/codec/stream.hpp>
#include <dacoexec/detail/huffman/stream.hpp>

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
  namespace huffman = dacoexec::detail::huffman;
  auto const root = std::filesystem::path(argv[1]);
  for (auto directory : { "huffman_decoder", "frame_parser", "round_trip", "ans_decoder", "arithmetic_decoder" }) {
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
      auto const shift = index * dacoexec::detail::support::bits_per_byte;
      envelope.push_back(static_cast<std::byte>(block.uncompressed_size_ >> shift));
    }
    for (auto length : block.lengths_) { envelope.push_back(static_cast<std::byte>(length)); }
    envelope.insert(envelope.end(), block.payload_.begin(), block.payload_.end());
    save(root / "huffman_decoder" / name, envelope);
    save(root / "round_trip" / name, input);
    for (unsigned table_log = 8; table_log <= 12; ++table_log) {
      auto archive =
        dacoexec::ans::compress(input, { .coder_ = dacoexec::ans::entropy_coder::tans, .table_log_ = table_log });
      save(root / "ans_decoder" / (name + "-tans-log-" + std::to_string(table_log) + ".dan"), archive);
    }

    constexpr std::array frame_block_sizes{ std::size_t{ 256 }, std::size_t{ 1024 } };
    for (auto block_size : frame_block_sizes) {
      std::vector<std::byte> archive;
      dacoexec::detail::io::iterator_sink sink{ std::back_inserter(archive) };
      static_cast<void>(huffman::compress_stream(input, sink, { .block_size_ = block_size }));
      save(root / "frame_parser" / (name + "-blocks-" + std::to_string(block_size)), archive);
      archive.clear();
      static_cast<void>(dacoexec::detail::codec::compress_stream<dacoexec::detail::arithmetic::stream_format>(
        input, sink, { .block_size_ = block_size }));
      save(root / "arithmetic_decoder" / (name + "-blocks-" + std::to_string(block_size) + ".dac"), archive);
    }
  }
  namespace arithmetic = dacoexec::detail::arithmetic;
  // Keep these counts in uint16 range for the direct-payload fuzz envelope.
  for (auto count : { std::size_t{ 64 }, std::size_t{ 40000 } }) {
    std::vector<std::byte> input(count, std::byte{ 'A' });
    // After the first rescale, introduce every previously unseen symbol.
    for (std::size_t index = 32513; index < count; ++index) { input[index] = static_cast<std::byte>(index % 256); }
    auto const name = count == 64 ? "adaptive-repeated" : "adaptive-rescale-unseen";
    std::vector<std::byte> archive;
    dacoexec::detail::io::iterator_sink sink{ std::back_inserter(archive) };
    static_cast<void>(dacoexec::detail::codec::compress_stream<arithmetic::stream_format>(
      input, sink, { .coder_ = arithmetic::entropy_coder::adaptive_range }));
    if (archive.size() <= arithmetic::archive_header_size
        || archive.at(arithmetic::archive_header_size) != std::byte{ 0x02 }) {
      throw std::runtime_error("Expected adaptive DAC1 frame");
    }
    save(root / "arithmetic_decoder" / (std::string(name) + ".dac"), archive);
    auto const payload = arithmetic::encode_adaptive_block(input);
    if (arithmetic::decode_adaptive_block(payload, count, count) != input) {
      throw std::runtime_error("Invalid adaptive fuzz seed");
    }
    std::vector<std::byte> envelope{ static_cast<std::byte>(count & 255U), static_cast<std::byte>(count >> 8U) };
    envelope.insert(envelope.end(), payload.begin(), payload.end());
    save(root / "arithmetic_decoder" / (std::string(name) + ".payload"), envelope);
  }
  return 0;
}
