#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <dacoexec/arithmetic.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {
auto read(std::filesystem::path const &path) -> std::vector<char>
{
  std::ifstream file(path, std::ios::binary);
  REQUIRE(file.is_open());
  return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
}// namespace

TEST_CASE("Committed DAC1 archives preserve decoding and exact encoding", "[arithmetic][compatibility]")
{
  auto const directory = std::filesystem::path(DACOEXEC_FIXTURE_DIR) / "dac1";
  using coder = dacoexec::arithmetic::entropy_coder;

  struct fixture
  {
    char const *name;
    coder coder_;
    unsigned tag;
    bool exact;
  };

  for (auto const &[name, selected, tag, exact] : { fixture{ "static-golden", coder::static_range, 0x01, true },
         fixture{ "adaptive-repeated", coder::adaptive_range, 0x02, true },
         fixture{ "adaptive-rescale-unseen", coder::adaptive_range, 0x02, true },
         fixture{ "binary-zeros", coder::binary, 0x03, true },
         fixture{ "binary-ones", coder::binary, 0x03, true },
         fixture{ "binary-uniform", coder::binary, 0x03, false },
         fixture{ "context-zeros", coder::context_binary, 0x04, true },
         fixture{ "context-mixed", coder::context_binary, 0x04, true },
         fixture{ "empty", coder::static_range, 0xff, true },
         fixture{ "raw", coder::static_range, 0x00, true } }) {
    CAPTURE(name);
    auto const wire = read(directory / (std::string(name) + ".dac"));
    auto const plain = read(directory / (std::string(name) + ".bin"));
    auto const expected = std::as_bytes(std::span(plain));
    auto const archive = std::as_bytes(std::span(wire));
    CHECK(std::ranges::equal(dacoexec::arithmetic::decompress(wire), expected));
    REQUIRE(archive.size() > 8);
    REQUIRE(std::to_integer<unsigned>(archive[8]) == tag);
    // The independent uniform binary seed deliberately bypasses RAW fallback.
    if (exact) { CHECK(std::ranges::equal(dacoexec::arithmetic::compress(plain, { .coder_ = selected }), archive)); }
  }
}
