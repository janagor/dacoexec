#include <dacoexec/huffman/stream.hpp>

[[nodiscard]] auto installed_stream_round_trip() -> bool
{
  constexpr std::size_t block_size = 4096;
  std::vector<std::byte> const input(block_size + 1, std::byte{ 'A' });
  std::vector<std::byte> archive;
  dacoexec::io::iterator_sink encoded_sink(std::back_inserter(archive));
  auto const encoded = dacoexec::huffman::compress_stream(input, encoded_sink, { .block_size_ = block_size });
  std::vector<std::byte> output;
  dacoexec::io::iterator_sink decoded_sink(std::back_inserter(output));
  auto const decoded = dacoexec::huffman::decompress_stream(archive, decoded_sink);
  constexpr std::uint64_t expected_frames = 2;
  return input == output && encoded.input_bytes_ == input.size() && encoded.output_bytes_ == archive.size()
         && decoded.input_bytes_ == archive.size() && decoded.output_bytes_ == input.size()
         && encoded.frames_ == expected_frames && decoded.frames_ == expected_frames;
}
