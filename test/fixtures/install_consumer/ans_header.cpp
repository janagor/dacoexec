#include <array>
#include <cstddef>
#include <dacoexec/ans.hpp>
#include <iterator>
#include <sstream>
#include <vector>

[[nodiscard]] auto installed_ans_round_trip() -> bool
{
  std::array<char, 5> input{ 'a', 'b', 'c', 'a', 'b' };
  std::vector<std::byte> archive;
  auto encoded = dacoexec::ans::compress_stream(input, std::back_inserter(archive), { .block_size_ = 2 });
  std::ostringstream output;
  auto decoded = dacoexec::ans::decompress_stream(archive, output);
  return output.str() == "abcab" && encoded.frames_ == 3 && decoded.output_bytes_ == input.size();
}
