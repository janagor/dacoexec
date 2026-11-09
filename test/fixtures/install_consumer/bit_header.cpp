#include <cstddef>
#include <dacoexec/bit.hpp>
#include <iterator>
#include <vector>

[[nodiscard]] auto installed_bit_round_trip() -> bool
{
  std::vector<std::byte> bytes;
  dacoexec::bit_writer writer{ std::back_inserter(bytes) };
  writer.write_bit(true);
  auto padding = writer.finish();
  dacoexec::bit_reader reader(bytes, bytes.size() * 8 - padding);
  return reader.read_bit() && reader.remaining_bits() == 0;
}
