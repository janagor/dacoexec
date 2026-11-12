#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <dacoexec/ans.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/huffman.hpp>
#include <utility>
#include <vector>

TEST_CASE("All seven codecs select compressed archive tags for zero input", "[codec][archive][compressed-tags]")
{
  std::vector<std::byte> const input(4096, std::byte{ 0 });
  auto const check = [&](auto const &archive, unsigned expected_tag, auto decode) {
    REQUIRE(archive.size() > 8);
    CHECK(std::to_integer<unsigned>(archive[8]) == expected_tag);
    CHECK(decode(archive) == input);
  };
  using arithmetic = dacoexec::arithmetic::entropy_coder;
  for (auto const &[coder, tag] : { std::pair{ arithmetic::static_range, 0x01U },
         std::pair{ arithmetic::adaptive_range, 0x02U },
         std::pair{ arithmetic::binary, 0x03U },
         std::pair{ arithmetic::context_binary, 0x04U } }) {
    CAPTURE(coder);
    check(dacoexec::arithmetic::compress(input, { .block_size_ = 1024, .coder_ = coder }),
      tag,
      [](auto const &archive) { return dacoexec::arithmetic::decompress(archive); });
  }
  using ans = dacoexec::ans::entropy_coder;
  for (auto const &[coder, tag] : { std::pair{ ans::rans, 0x01U }, std::pair{ ans::tans, 0x02U } }) {
    CAPTURE(coder);
    check(dacoexec::ans::compress(input, { .block_size_ = 1024, .coder_ = coder }), tag, [](auto const &archive) {
      return dacoexec::ans::decompress(archive);
    });
  }
  check(dacoexec::huffman::compress(input, { .block_size_ = 1024 }), 0x01U, [](auto const &archive) {
    return dacoexec::huffman::decompress(archive);
  });
}
