#ifndef DACOEXEC_FUZZ_ERROR_POLICY_HPP
#define DACOEXEC_FUZZ_ERROR_POLICY_HPP

#include <dacoexec/error.hpp>

namespace dacoexec::fuzz {
// Archive fuzzing may reject framing, payloads, checksums, or configured limits.
[[nodiscard]] inline auto expected_archive_error(std::error_code code) noexcept -> bool
{
  return code == errc::invalid_archive || code == errc::truncated_archive || code == errc::invalid_frame
         || code == errc::invalid_payload || code == errc::checksum_mismatch || code == errc::output_limit_exceeded;
}

// The Huffman envelope lets the fuzzer supply an output size above its limit.
[[nodiscard]] inline auto expected_huffman_block_error(std::error_code code) noexcept -> bool
{ return code == errc::invalid_payload || code == errc::output_limit_exceeded; }

// The ANS block harness supplies fixed valid models and sizes below its limit.
[[nodiscard]] inline auto expected_ans_block_error(std::error_code code) noexcept -> bool
{ return code == errc::invalid_payload; }
}// namespace dacoexec::fuzz
#endif
