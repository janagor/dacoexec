#ifndef DACOEXEC_IO_OUTPUT_SINK_HPP
#define DACOEXEC_IO_OUTPUT_SINK_HPP

#include <dacoexec/detail/error.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ostream>
#include <span>
#include <utility>

namespace dacoexec::io {
// Failure may leave a written prefix. Callers propagate failures without retry.
template<class S>
concept byte_output_sink = requires(S &sink, std::span<const std::byte> bytes) {
  { sink.write(bytes) } -> std::same_as<void>;
};
class ostream_sink
{
public:
  explicit ostream_sink(std::ostream &stream) : stream_(stream) {}
  auto write(std::span<const std::byte> bytes) -> void
  {
    constexpr std::size_t buffer_size = 4096;
    std::array<char, buffer_size> buffer{};
    while (!bytes.empty()) {
      auto const count = std::min(bytes.size(), buffer.size());
      auto const chunk = bytes.first(count);
      std::ranges::transform(chunk, buffer.begin(), [](std::byte byte) -> char {
        return static_cast<char>(std::to_integer<unsigned char>(byte));
      });
      stream_.get().write(buffer.data(), static_cast<std::streamsize>(count));
      if (!stream_.get()) { throw stream_error("Output stream write failed"); }
      bytes = bytes.subspan(count);
    }
  }
private:
  std::reference_wrapper<std::ostream> stream_;
};
template<class I>
class iterator_sink
{
public:
  explicit iterator_sink(I output) : output_(std::move(output)) {}
  auto write(std::span<const std::byte> bytes) -> void
  { for (auto byte : bytes) { *output_++ = byte; } }
private:
  I output_;
};
}// namespace dacoexec::io
#endif // DACOEXEC_IO_OUTPUT_SINK_HPP
