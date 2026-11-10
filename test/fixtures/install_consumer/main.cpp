#include <array>
#include <cstddef>
#include <dacoexec/dacoexec.hpp>
#include <vector>
[[nodiscard]] auto installed_huffman_round_trip() -> bool;
[[nodiscard]] auto installed_ans_round_trip() -> bool;
[[nodiscard]] auto installed_arithmetic_round_trip() -> bool;
[[nodiscard]] auto installed_views_round_trip() -> bool;
[[nodiscard]] auto installed_bit_round_trip() -> bool;

int main()
{
  std::array<char, 4> input{ 'a', 'b', 'a', 'b' };
  auto archive = dacoexec::huffman::compress(input);
  auto output = dacoexec::huffman::decompress(archive);
  return output.size() == input.size() && installed_huffman_round_trip() && installed_ans_round_trip()
             && installed_arithmetic_round_trip() && installed_views_round_trip() && installed_bit_round_trip()
           ? 0
           : 1;
}
