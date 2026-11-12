#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <dacoexec/detail/arithmetic/binary/codec.hpp>
#include <dacoexec/detail/arithmetic/binary/context/codec.hpp>
#include <dacoexec/detail/arithmetic/range/adaptive/codec.hpp>
#include <dacoexec/detail/arithmetic/range/static/codec.hpp>
#include <span>
#include <vector>

extern "C" auto LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size) -> int
{
  if (size < 2) { return 0; }
  auto const input = std::as_bytes(std::span(data + 1, std::min(size - 1, std::size_t{ 4096 })));
  namespace impl = dacoexec::detail::arithmetic;
  std::vector<std::byte> restored;
  // One coder per mutation; these adapters never select RAW, even for noise.
  // Keep this campaign separate from archive framing and fallback coverage.
  switch (data[0] % 4U) {
  case 0: {
    auto const model = impl::make_static_model(input);
    restored = impl::decode_block(impl::encode_block(input, model), model, input.size(), 4096);
    break;
  }
  case 1:
    restored = impl::decode_adaptive_block(impl::encode_adaptive_block(input), input.size(), 4096);
    break;
  case 2: {
    auto const model = impl::make_binary_model(input);
    restored = impl::decode_binary_block(impl::encode_binary_block(input, model), model, input.size(), 4096);
    break;
  }
  case 3:
    restored = impl::decode_context_binary_block(impl::encode_context_binary_block(input), input.size(), 4096);
    break;
  }
  if (!std::ranges::equal(input, restored)) { std::abort(); }
  return 0;
}
