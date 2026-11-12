#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <dacoexec/ans.hpp>
#include <dacoexec/arithmetic.hpp>
#include <dacoexec/huffman.hpp>
#include <span>

namespace {
template<class Encode, class Decode>
void check(std::span<std::byte const> input, Encode encode, Decode decode, unsigned expected_tag)
{
  auto const archive = encode(input);
  // The committed zero seed must exercise each compressed archive dispatch.
  if (input.size() >= 1024 && std::ranges::all_of(input, [](auto byte) { return byte == std::byte{ 0 }; })) {
    if (archive.size() <= 8 || std::to_integer<unsigned>(archive[8]) != expected_tag) { std::abort(); }
  }
  auto const restored = decode(archive);
  if (!std::ranges::equal(input, restored)) { std::abort(); }
}
}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const input = std::as_bytes(std::span(data, std::min(size, std::size_t{ 4096 })));
  // Keep frame count bounded while covering partial frames and model resets.
  auto const block_size = input.empty() ? 1024U : 1024U << (std::to_integer<unsigned>(input.front()) % 4U);
  for (auto coder : { dacoexec::arithmetic::entropy_coder::static_range,
         dacoexec::arithmetic::entropy_coder::adaptive_range,
         dacoexec::arithmetic::entropy_coder::binary,
         dacoexec::arithmetic::entropy_coder::context_binary }) {
    check(
      input,
      [=](
        auto source) { return dacoexec::arithmetic::compress(source, { .block_size_ = block_size, .coder_ = coder }); },
      [](auto const &archive) {
        return dacoexec::arithmetic::decompress(archive, { .max_frame_size_ = 65536, .max_output_bytes_ = 65536 });
      },
      coder == dacoexec::arithmetic::entropy_coder::static_range     ? 1U
      : coder == dacoexec::arithmetic::entropy_coder::adaptive_range ? 2U
      : coder == dacoexec::arithmetic::entropy_coder::binary         ? 3U
                                                                     : 4U);
  }
  for (auto coder : { dacoexec::ans::entropy_coder::rans, dacoexec::ans::entropy_coder::tans }) {
    check(
      input,
      [=](auto source) { return dacoexec::ans::compress(source, { .block_size_ = block_size, .coder_ = coder }); },
      [](auto const &archive) { return dacoexec::ans::decompress(archive); },
      coder == dacoexec::ans::entropy_coder::rans ? 1U : 2U);
  }
  check(
    input,
    [=](auto source) { return dacoexec::huffman::compress(source, { .block_size_ = block_size }); },
    [](auto const &archive) { return dacoexec::huffman::decompress(archive); },
    1U);
  return 0;
}
