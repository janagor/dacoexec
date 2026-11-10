#include "error_policy.hpp"
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/ans/rans/codec.hpp>
#include <dacoexec/detail/ans/stream.hpp>
#include <dacoexec/error.hpp>
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
    static_cast<void>(dacoexec::detail::ans::decompress_stream(
      bytes, sink, { .max_frame_size_ = output_limit, .max_output_bytes_ = output_limit }));
  } catch (dacoexec::exception const &error) {
    if (!dacoexec::fuzz::expected_archive_error(error.code())) { throw; }
  }
  if (bytes.empty()) { return 0; }
  dacoexec::detail::model::frequency_table counts{};
  counts[0] = 1;
  counts[1] = 3;
  auto byte_model = dacoexec::detail::model::make_model(counts, 10);
  try {
    static_cast<void>(dacoexec::detail::ans::tans::decode_block(bytes, byte_model, 1024, output_limit));
  } catch (dacoexec::exception const &error) {
    if (!dacoexec::fuzz::expected_ans_block_error(error.code())) { throw; }
  }
  constexpr std::size_t symbols = 1024;
  try {
    static_cast<void>(dacoexec::detail::ans::rans::decode_block(bytes, byte_model, symbols, output_limit));
  } catch (dacoexec::exception const &error) {
    if (!dacoexec::fuzz::expected_ans_block_error(error.code())) { throw; }
  }
  return 0;
}
