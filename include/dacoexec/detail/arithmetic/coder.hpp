//! Shared arithmetic variant selector and availability validation.
#ifndef DACOEXEC_ARITHMETIC_CODER_HPP
#define DACOEXEC_ARITHMETIC_CODER_HPP
#include <cstdint>
#include <dacoexec/error.hpp>

namespace dacoexec::arithmetic {
enum class entropy_coder : std::uint8_t { static_range = 0, adaptive_range = 1, binary = 2, context_binary = 3 };
}

namespace dacoexec::detail::arithmetic {
using ::dacoexec::arithmetic::entropy_coder;

inline auto validate_coder(entropy_coder coder) -> void
{
  if (coder != entropy_coder::static_range && coder != entropy_coder::adaptive_range && coder != entropy_coder::binary
      && coder != entropy_coder::context_binary) {
    throw ::dacoexec::exception(::dacoexec::errc::unsupported_coder, "Unsupported arithmetic entropy coder");
  }
}
}// namespace dacoexec::detail::arithmetic
#endif
