#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/arithmetic/binary/decoder.hpp>
#include <span>

namespace {
constexpr std::size_t envelope_size = 6;

auto read_u16(std::span<std::byte const> bytes) -> std::uint32_t
{
  constexpr unsigned byte_bits = 8;
  return std::to_integer<std::uint32_t>(bytes[0]) | (std::to_integer<std::uint32_t>(bytes[1]) << byte_bits);
}
}// namespace

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  auto const bytes = std::as_bytes(std::span(data, size));
  if (bytes.size() < envelope_size) { return 0; }
  namespace arithmetic = dacoexec::detail::arithmetic;
  // A LE uint16 bounds work to 65535 bits without allocating decoded output.
  auto const bit_count = read_u16(bytes.first(2));
  auto const zero = 1U + read_u16(bytes.subspan(2, 2)) % (arithmetic::binary_max_total - 1);
  auto const one = 1U + read_u16(bytes.subspan(4, 2)) % (arithmetic::binary_max_total - zero);
  arithmetic::binary_model const probabilities{ zero, one };
  try {
    arithmetic::binary_arithmetic_decoder decoder(bytes.subspan(envelope_size));
    for (std::uint32_t index = 0; index < bit_count; ++index) { static_cast<void>(decoder.decode(probabilities)); }
    decoder.finish();
  } catch (dacoexec::exception const &error) {
    // The harness always supplies a valid model and active decoder. Only a
    // malformed or truncated payload is an expected rejection.
    if (error.code() != dacoexec::errc::invalid_payload) { throw; }
  }
  return 0;
}
