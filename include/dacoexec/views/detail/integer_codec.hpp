//! \file
//! Fixed-storage integer codeword state and codec configuration.

#ifndef DACOEXEC_VIEWS_DETAIL_INTEGER_CODEC_HPP
#define DACOEXEC_VIEWS_DETAIL_INTEGER_CODEC_HPP

#include <dacoexec/integer_coding/sequence.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace dacoexec::views::detail {

// Delta needs at most three binary segments. Unary codes use a separate
// counter, so even a codeword longer than uint64_t can be emitted lazily.
inline constexpr std::size_t integer_segment_capacity = 3;

struct integer_codeword
{
  auto append(std::uint64_t value, unsigned width) -> void
  {
    if (width != 0U) { segments_.at(size_++) = { .value_ = value, .bit_count_ = width }; }
  }

  [[nodiscard]] auto bit() const -> bool
  {
    if (unary_ != 0U) { return true; }
    if (terminator_) { return false; }
    auto const &part = segments_.at(index_);
    return ((part.value_ >> (part.bit_count_ - 1U)) & 1U) != 0U;
  }

  auto advance() -> void
  {
    if (unary_ != 0U) {
      --unary_;
    } else if (terminator_) {
      terminator_ = false;
    } else if (--segments_.at(index_).bit_count_ == 0U) {
      ++index_;
    }
  }

  [[nodiscard]] auto empty() const noexcept -> bool { return unary_ == 0U && !terminator_ && index_ == size_; }

  std::array<integer_coding::detail::integer_bit_sequence, integer_segment_capacity> segments_{};
  std::size_t size_ = 0;
  std::size_t index_ = 0;
  std::uint64_t unary_ = 0;
  bool terminator_ = false;
};

// Keep the same codec interface as parameterized codecs.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
struct gamma_codec
{
  [[nodiscard]] auto encode(std::uint64_t value) const -> integer_codeword
  {
    (void)integer_coding::gamma_bit_length(value);
    auto const width = static_cast<unsigned>(std::bit_width(value));
    integer_codeword word;
    word.append(0U, width - 1U);
    word.append(value, width);
    return word;
  }

  template<integer_coding::bit_input Reader> [[nodiscard]] auto decode(Reader &reader) const -> std::uint64_t
  { return integer_coding::read_gamma(reader); }
};

struct delta_codec
{
  [[nodiscard]] auto encode(std::uint64_t value) const -> integer_codeword
  {
    (void)integer_coding::delta_bit_length(value);
    auto const width = static_cast<unsigned>(std::bit_width(value));
    auto word = gamma_codec{}.encode(width);
    word.append(value, width - 1U);
    return word;
  }

  template<integer_coding::bit_input Reader> [[nodiscard]] auto decode(Reader &reader) const -> std::uint64_t
  { return integer_coding::read_delta(reader); }
};

// NOLINTEND(readability-convert-member-functions-to-static)

struct golomb_codec
{
  [[nodiscard]] auto encode(std::uint64_t value) const -> integer_codeword
  {
    auto const width = static_cast<unsigned>(std::bit_width(divisor_ - 1U));
    auto const cutoff = (std::uint64_t{ 1 } << width) - divisor_;
    auto const remainder = value % divisor_;
    integer_codeword word;
    word.unary_ = value / divisor_;
    word.terminator_ = true;
    if (remainder < cutoff) {
      word.append(remainder, width - 1U);
    } else {
      word.append(remainder + cutoff, width);
    }
    return word;
  }

  template<integer_coding::bit_input Reader> [[nodiscard]] auto decode(Reader &reader) const -> std::uint64_t
  { return integer_coding::read_golomb(reader, divisor_, limits_); }

  std::uint32_t divisor_ = 1;
  integer_coding::decode_limits limits_{};
};

struct rice_codec
{
  [[nodiscard]] auto encode(std::uint64_t value) const -> integer_codeword
  {
    integer_codeword word;
    word.unary_ = value >> shift_;
    word.terminator_ = true;
    word.append(value, shift_);
    return word;
  }

  template<integer_coding::bit_input Reader> [[nodiscard]] auto decode(Reader &reader) const -> std::uint64_t
  { return integer_coding::read_rice(reader, shift_, limits_); }

  unsigned shift_ = 0;
  integer_coding::decode_limits limits_{};
};

}// namespace dacoexec::views::detail

#endif// DACOEXEC_VIEWS_DETAIL_INTEGER_CODEC_HPP
