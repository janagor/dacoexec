#include <dacoexec/dacoexec.hpp>
#include <dacoexec/views/blocks.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <span>
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
    if (block.sequence != sequence) { return 1; }
    ++sequence;
    sizes.push_back(block.bytes.size());
    flattened.insert(flattened.end(), block.bytes.begin(), block.bytes.end());
  }

  std::vector<std::size_t> const expected_sizes{ 4, 1 };
  if (sizes != expected_sizes || flattened != input) { return 1; }

  std::cout << "dacoexec install consumer ok\n";
  return 0;
}
