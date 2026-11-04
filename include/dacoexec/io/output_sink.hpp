//! \file
//! Incremental byte sinks for streams and output iterators.

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
/**
 * A sink accepting a span of bytes through write() returning void.
 *
 * Writes are incremental: failure may leave a written prefix. Consumers propagate
 * failures without retry or rollback. The concept checks syntax, not atomicity.
 */
template<class S>
concept byte_output_sink = requires(S &sink, std::span<std::byte const> bytes) {
  { sink.write(bytes) } -> std::same_as<void>;
};
/**
 * Non-owning adapter for a std::ostream, which must outlive the adapter.
 *
 * @see byte_output_sink
 */
class ostream_sink
{
public:
  /**
   * Binds the destination stream without taking ownership.
   */
  explicit ostream_sink(std::ostream &stream) : stream_(stream) {}
  /**
   * Writes bytes in bounded chunks and checks the stream after each chunk.
   *
   * @throws stream_error If the stream reports failure.
   * Stream exceptions propagate. An accepted prefix is not rolled back.
   */
  auto write(std::span<std::byte const> bytes) -> void
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
/**
 * Owns an output-iterator cursor and assigns std::byte values through it.
 *
 * The destination storage must remain valid. Iterator failures propagate and
 * may leave a written prefix; the adapter offers no retry or rollback.
 *
 * @code
 * io::iterator_sink sink{std::back_inserter(bytes)};
 * @endcode
 */
template<class I> class iterator_sink
{
public:
  /**
   * Takes the output cursor by value and moves it into the adapter.
   */
  explicit iterator_sink(I output) : output_(std::move(output)) {}
  /**
   * Assigns each byte and advances the stored cursor. Iterator exceptions propagate.
   */
  auto write(std::span<std::byte const> bytes) -> void
  {
    for (auto byte : bytes) { *output_++ = byte; }
  }

private:
  I output_;
};
}// namespace dacoexec::io
#endif// DACOEXEC_IO_OUTPUT_SINK_HPP
