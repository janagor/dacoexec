#include "error_policy.hpp"
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/archive_format.hpp>
#include <dacoexec/detail/codec/stream.hpp>
#include <span>

namespace {
namespace arithmetic = dacoexec::detail::arithmetic;
constexpr std::size_t output_limit = 64U << 10U;

struct discard_sink
{
  auto write(std::span<std::byte const>) -> void {}
};

// Reserved DAC1 variants are expected rejections. Argument errors are not:
// malformed metadata must be normalized at the archive boundary.
auto expected_archive_error(dacoexec::exception const &error) -> bool
{ return dacoexec::fuzz::expected_archive_error(error.code()) || error.code() == dacoexec::errc::unsupported_coder; }
}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const bytes = std::as_bytes(std::span(data, size));
  discard_sink sink;
  try {
    static_cast<void>(dacoexec::detail::codec::decompress_stream<arithmetic::stream_format>(
      bytes, sink, { .max_frame_size_ = output_limit, .max_output_bytes_ = output_limit }));
  } catch (dacoexec::exception const &error) {
    if (!expected_archive_error(error)) { throw; }
  }

  if (bytes.size() >= arithmetic::frame_header_size) {
    try {
      static_cast<void>(arithmetic::read_frame_header(bytes.first<arithmetic::frame_header_size>(), output_limit));
    } catch (dacoexec::exception const &error) {
      if (!expected_archive_error(error)) { throw; }
    }
  }
  try {
    static_cast<void>(arithmetic::deserialize_model(bytes));
  } catch (dacoexec::exception const &error) {
    if (error.code() != dacoexec::errc::invalid_payload) { throw; }
  }

  // Decode arbitrary (including truncated) payloads with valid fixed intervals.
  static auto const probabilities = [] {
    arithmetic::static_model::table_type frequencies{};
    frequencies.at(0) = 1;
    frequencies.at(1) = 3;
    return arithmetic::static_model{ frequencies, 2 };
  }();
  try {
    static_cast<void>(arithmetic::decode_block(bytes, probabilities, 256, output_limit));
  } catch (dacoexec::exception const &error) {
    if (error.code() != dacoexec::errc::invalid_payload) { throw; }
  }
  return 0;
}
