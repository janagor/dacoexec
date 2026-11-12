//! \file
//! Shared adaptive byte-model intervals and deterministic frequency rules.
#ifndef DACOEXEC_ARITHMETIC_ADAPTIVE_MODEL_COMMON_HPP
#define DACOEXEC_ARITHMETIC_ADAPTIVE_MODEL_COMMON_HPP

#include <dacoexec/detail/arithmetic/range/common.hpp>
#include <dacoexec/detail/model/frequency.hpp>
#include <dacoexec/error.hpp>

#include <array>
#include <cstdint>

namespace dacoexec::detail::arithmetic {
using adaptive_frequency_table = std::array<std::uint32_t, model::alphabet_size>;
inline constexpr std::uint32_t adaptive_rescale_threshold = 1U << 15U;
static_assert(adaptive_rescale_threshold + 1 <= range_max_total);

struct adaptive_interval
{
  std::uint32_t cumulative_;
  std::uint32_t frequency_;
  std::uint32_t total_;
};

inline void rescale_adaptive_frequencies(adaptive_frequency_table &frequencies)
{
  for (auto &frequency : frequencies) { frequency = (frequency + 1U) / 2U; }
}

inline void validate_adaptive_slot(std::uint32_t slot, std::uint32_t total)
{
  if (slot >= total) {
    throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Cumulative slot outside adaptive model");
  }
}
}// namespace dacoexec::detail::arithmetic
#endif// DACOEXEC_ARITHMETIC_ADAPTIVE_MODEL_COMMON_HPP
