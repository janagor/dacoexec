//! Deterministic FSE-style tables; encoder storage is linear in table size.
#ifndef DACOEXEC_ANS_TANS_TABLE_HPP
#define DACOEXEC_ANS_TANS_TABLE_HPP
#include <array>
#include <bit>
#include <cstdint>
#include <dacoexec/detail/ans/model.hpp>
#include <vector>

namespace dacoexec::detail::ans::tans {
inline constexpr unsigned min_table_log = 8;
inline constexpr unsigned max_table_log = 12;
inline constexpr unsigned default_table_log = 10;
inline constexpr unsigned transform_shift = 16;

inline auto validate_precision(unsigned precision) -> void
{
  if (precision < min_table_log || precision > max_table_log) { throw std::invalid_argument("Invalid tANS table log"); }
}

struct decode_entry
{
  std::uint16_t baseline_;
  std::uint8_t symbol_;
  std::uint8_t num_bits_;
};

static_assert(sizeof(decode_entry) == 4);

class tables
{
public:
  explicit tables(frequency_model<alphabet_size> const &model) : model_(model)
  {
    validate_precision(model.precision_bits());
    auto const size = model.total();
    decode_.resize(size);
    encode_.resize(size);
    auto next = model.frequencies();
    std::uint32_t position = 0;
    auto const step = (size >> 1U) + (size >> 3U) + 3U;
    for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
      auto const frequency = model.frequencies().at(symbol);
      if (frequency == 0) { continue; }
      // For x in [M, 2M), the output width is max_bits - 1 below
      // frequency * 2^max_bits, and max_bits at or above that threshold.
      // Biasing x before a fixed shift implements this choice without a loop.
      // bit_width(frequency - 1) also handles singleton and full-table symbols.
      auto const max_bits = model.precision_bits() + 1U - static_cast<unsigned>(std::bit_width(frequency - 1U));
      transforms_.at(symbol) = { .delta_nb_bits_ = (max_bits << transform_shift) - (frequency << max_bits),
        .delta_find_state_ =
          static_cast<std::int32_t>(model.cumulative().at(symbol)) - static_cast<std::int32_t>(frequency) };
      for (std::uint32_t slot = 0; slot < frequency; ++slot) {
        decode_.at(position).symbol_ = static_cast<std::uint8_t>(symbol);
        position = (position + step) & (size - 1U);
      }
    }
    for (std::uint32_t state = 0; state < size; ++state) {
      auto &entry = decode_.at(state);
      auto const symbol = entry.symbol_;
      auto const value = next.at(symbol)++;
      entry.num_bits_ =
        static_cast<std::uint8_t>(model.precision_bits() - (static_cast<unsigned>(std::bit_width(value)) - 1U));
      entry.baseline_ = static_cast<std::uint16_t>((value << entry.num_bits_) - size);
      encode_.at(model.cumulative().at(symbol) + value - model.frequencies().at(symbol)) =
        static_cast<std::uint16_t>(state);
    }
  }

  struct transition
  {
    std::uint32_t state_;
    unsigned num_bits_;
    std::uint32_t bits_;
  };

  [[nodiscard]] auto encode(std::uint32_t state, unsigned symbol) const -> transition
  {
    auto const &transform = transforms_.at(symbol);
    // Every present symbol has a positive bias at supported table logs;
    // zero is reserved for symbols absent from the model.
    if (transform.delta_nb_bits_ == 0) { throw std::invalid_argument("Absent tANS symbol"); }
    auto const value = state + model_.total();
    auto const bits = (value + transform.delta_nb_bits_) >> transform_shift;
    auto const index = static_cast<std::int32_t>(value >> bits) + transform.delta_find_state_;
    return {
      .state_ = encode_.at(static_cast<std::size_t>(index)), .num_bits_ = bits, .bits_ = state & ((1U << bits) - 1U)
    };
  }

  [[nodiscard]] auto total() const noexcept -> std::uint32_t { return model_.total(); }

  [[nodiscard]] auto decode(std::uint32_t state) const -> decode_entry const & { return decode_.at(state); }

private:
  struct symbol_transform
  {
    std::uint32_t delta_nb_bits_ = 0;
    std::int32_t delta_find_state_ = 0;
  };

  frequency_model<alphabet_size> model_;
  std::array<symbol_transform, alphabet_size> transforms_{};
  std::vector<decode_entry> decode_;
  std::vector<std::uint16_t> encode_;
};
}// namespace dacoexec::detail::ans::tans
#endif
