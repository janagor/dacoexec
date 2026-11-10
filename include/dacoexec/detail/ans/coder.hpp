//! Shared public algorithm selector and internal validation.
#ifndef DACOEXEC_ANS_CODER_HPP
#define DACOEXEC_ANS_CODER_HPP
#include <dacoexec/detail/ans/tans/table.hpp>
#include <dacoexec/error.hpp>

namespace dacoexec::ans {
enum class entropy_coder : std::uint8_t { rans, tans };
}

namespace dacoexec::detail::ans {
using ::dacoexec::ans::entropy_coder;

inline auto validate_coder(entropy_coder coder, unsigned table_log) -> void
{
  switch (coder) {
  case entropy_coder::rans:
    return;
  case entropy_coder::tans:
    tans::validate_precision(table_log);
    return;
  }
  throw ::dacoexec::exception(::dacoexec::errc::unsupported_coder, "Unsupported ANS entropy coder");
}
}// namespace dacoexec::detail::ans
#endif
