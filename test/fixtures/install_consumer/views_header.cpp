#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <dacoexec/views.hpp>
#include <iterator>
#include <ranges>
#include <vector>

[[nodiscard]] auto installed_views_round_trip() -> bool
{
  std::array<std::uint64_t, 3> input{ 1, 2, 13 };
  auto output = input | dacoexec::views::encode::gamma() | dacoexec::views::decode::gamma(input.size());
  std::vector<std::uint64_t> values;
  for (auto value : output) { values.push_back(value); }
  std::array<char, 5> text{ 'h', 'e', 'l', 'l', 'o' };
  auto huffman = text | dacoexec::views::encode::huffman({ .block_size_ = 2 })
                 | dacoexec::views::decode::huffman({ .max_output_bytes_ = text.size() });
  auto ans = text | dacoexec::views::encode::ans({ .coder_ = dacoexec::ans::entropy_coder::tans })
             | dacoexec::views::decode::ans({ .max_output_bytes_ = text.size() });
  std::vector<std::byte> first;
  std::vector<std::byte> second;
  std::ranges::copy(huffman, std::back_inserter(first));
  std::ranges::copy(ans, std::back_inserter(second));
  return std::ranges::equal(values, input) && first == second && first.size() == text.size();
}
