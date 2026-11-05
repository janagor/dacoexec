#include <dacoexec/detail/error.hpp>
#include <dacoexec/huffman/decode_view.hpp>
#include <dacoexec/huffman/stream.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace {
namespace huffman = dacoexec::huffman;
constexpr std::size_t input_limit = 65536;
constexpr std::size_t output_limit = 256U * 1024U;
struct checking_sink
{
  std::size_t count_ = 0;
  auto write(std::span<std::byte const> bytes) -> void
  {
    if (bytes.size() > output_limit - count_) { std::abort(); }
    count_ += bytes.size();
  }
};

auto parse(std::span<std::uint8_t const> input) -> void
{
  checking_sink sink;
  try {
    auto const statistics =
      huffman::decompress_stream(input, sink, { .max_frame_size_ = input_limit, .max_output_bytes_ = output_limit });
    if (statistics.input_bytes_ != input.size() || statistics.output_bytes_ != sink.count_) { std::abort(); }
  } catch (dacoexec::format_error const &) {
  } catch (huffman::decode_error const &) {}
}
}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const input = std::span(data, size).first(std::min(size, input_limit));
  parse(input);
  return 0;
}
