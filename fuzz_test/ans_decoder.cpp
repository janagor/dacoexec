#include <cstddef>
#include <cstdint>
#include <dacoexec/ans/stream.hpp>
#include <span>
#include <stdexcept>

namespace {
struct discard_sink
{
  auto write(std::span<std::byte const>) -> void {}
};

constexpr std::size_t output_limit = 64U << 10U;
}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const bytes = std::as_bytes(std::span(data, size));
  discard_sink sink;
  try {
    static_cast<void>(dacoexec::ans::decompress_stream(
      bytes, sink, { .max_frame_size_ = output_limit, .max_output_bytes_ = output_limit }));
  } catch (dacoexec::format_error const &) {
  } catch (dacoexec::ans::decode_error const &) {}
  if (bytes.empty()) { return 0; }
  constexpr std::size_t symbols = 1024;
  auto const model = dacoexec::ans::make_binary_model(1, 1);
  try {
    static_cast<void>(dacoexec::ans::detail::decode(bytes, model, symbols, output_limit));
  } catch (dacoexec::ans::decode_error const &) {}
  return 0;
}
