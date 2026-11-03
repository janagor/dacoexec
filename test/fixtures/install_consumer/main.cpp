#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/huffman/frequency.hpp>
#include <dacoexec/huffman/tree.hpp>
#include <dacoexec/views/bits.hpp>
#include <dacoexec/views/buffered_bytes.hpp>

#include <dacoexec/dacoexec.hpp>
#include <dacoexec/views/blocks.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

int main()
{
#if defined(_MSVC_LANG)
  static_assert(_MSVC_LANG >= 202002L);
#else
  static_assert(__cplusplus >= 202002L);
#endif
  static_assert(std::ranges::input_range<std::span<std::byte const>>);
  static_assert(dacoexec::byte_input_range<std::vector<std::byte>>);

  std::vector<std::byte> const input{
    std::byte{ 1 },
    std::byte{ 2 },
    std::byte{ 3 },
    std::byte{ 4 },
    std::byte{ 5 },
  };

  std::vector<std::size_t> sizes;
  std::uint64_t sequence = 0;
  std::vector<std::byte> flattened;
  for (auto block : input | dacoexec::views::blocks(4)) {
    if (block.sequence_ != sequence) { return 1; }
    ++sequence;
    sizes.push_back(block.bytes_.size());
    flattened.insert(flattened.end(), block.bytes_.begin(), block.bytes_.end());
  }

  std::vector<std::size_t> const expected_sizes{ 4, 1 };
  if (sizes != expected_sizes || flattened != input) { return 1; }

  std::vector<std::byte> packed;
  dacoexec::bit_writer round_trip{ std::back_inserter(packed) };
  for (bool bit : input | dacoexec::views::bits()) { round_trip.write_bit(bit); }
  if (round_trip.finish() != 0 || packed != input) { return 2; }

  std::vector<std::byte> codeword;
  dacoexec::bit_writer codes{ std::back_inserter(codeword) };
  codes.write_bits(0b101U, 3U);
  codes.write_bits(0b11001U, 5U);
  if (codes.finish() != 0 || codeword.size() != 1 || codeword[0] != std::byte{ 0xB9 }) { return 3; }

  std::array<bool, 4> const flags{ true, false, true, true };
  std::vector<std::byte> copied;
  dacoexec::bit_writer copy_writer{ std::back_inserter(copied) };
  std::ranges::copy(flags, copy_writer.output_iterator());
  if (copy_writer.finish() != 4 || copied.size() != 1 || copied[0] != std::byte{ 0xB0 }) { return 4; }

  std::vector<std::byte> windowed;
  dacoexec::bit_writer buffered_writer{ std::back_inserter(windowed) };
  for (bool bit : input | dacoexec::views::buffered_bytes(2) | dacoexec::views::bits()) {
    buffered_writer.write_bit(bit);
  }
  if (buffered_writer.finish() != 0 || windowed != input) { return 5; }

  std::string_view const sample = "AAAAABBCD";
  auto const frequencies = dacoexec::huffman::count_frequencies(sample);
  auto const lengths = dacoexec::huffman::build_code_lengths(frequencies);
  unsigned char const letter_a = static_cast<unsigned char>('A');
  if (frequencies.at(letter_a) != 5 || lengths.at(letter_a) != 1) { return 6; }

  std::cout << "dacoexec install consumer ok\n";
  return 0;
}
