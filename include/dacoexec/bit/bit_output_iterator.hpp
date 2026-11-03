#pragma once

#include <dacoexec/detail/byte.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dacoexec {
namespace detail {

  // Largest codeword accepted by bit_writer. A 32-bit mask is built with a
  // 64-bit shift so a 32-bit integer is never shifted by 32.
  inline constexpr unsigned kMaxCodewordBits = 32U;

  enum class CodewordStatus : std::uint8_t {
    kOk,
    kCountTooLarge,
    kValueTooWide,
  };

  // Mask of the low `bit_count` bits. `bit_count` must be 0..32.
  [[nodiscard]] constexpr std::uint32_t LowBitMask(unsigned bit_count) noexcept
  { return static_cast<std::uint32_t>((std::uint64_t{ 1 } << bit_count) - std::uint64_t{ 1 }); }

  // `bit_count` must be 0..32 for kOk. Wider counts are rejected. A value is
  // too wide when any bit above that width is set. Zero width allows only zero.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  [[nodiscard]] constexpr CodewordStatus CheckCodeword(std::uint32_t value, unsigned bit_count) noexcept
  {
    if (bit_count > kMaxCodewordBits) { return CodewordStatus::kCountTooLarge; }
    std::uint32_t const mask = LowBitMask(bit_count);
    if ((value & ~mask) != 0U) { return CodewordStatus::kValueTooWide; }
    return CodewordStatus::kOk;
  }

}// namespace detail

template<class Out>
  requires std::output_iterator<Out, std::byte>
class bit_writer;// NOLINT(readability-identifier-naming)

// Output iterator that writes each assigned bool as one bit of a bit_writer.
//
// Copying the iterator copies the handle, not the packing state. Every copy
// refers to the same writer. Incrementing does not emit a byte. The iterator
// must not outlive the writer, and a move of the writer invalidates it.
template<class Out>
  requires std::output_iterator<Out, std::byte>
class bit_output_iterator// NOLINT(readability-identifier-naming)
{
public:
  using iterator_category = std::output_iterator_tag;
  using iterator_concept = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  class Proxy
  {
  public:
    // Const assignment is required by std::indirectly_writable.
    // NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
    Proxy const &operator=(bool bit) const;

  private:
    friend class bit_output_iterator;

    explicit Proxy(bit_writer<Out> *writer) noexcept : writer_(writer) {}

    bit_writer<Out> *writer_ = nullptr;
  };

  explicit bit_output_iterator(bit_writer<Out> &writer) noexcept : writer_(&writer) {}

  [[nodiscard]] Proxy operator*() const noexcept { return Proxy{ writer_ }; }

  bit_output_iterator &operator++() noexcept { return *this; }

  bit_output_iterator operator++(int) noexcept { return *this; }

private:
  bit_writer<Out> *writer_ = nullptr;
};

// Packs bits into bytes, most significant bit first.
//
// write_bit writes one bit. write_bits writes the low `bit_count` bits of
// `value`, highest of those bits first. `bit_count` is 0 through 32. A
// zero-length write of a zero value does nothing. Counts above 32, and values
// with bits set outside the declared width, throw std::invalid_argument.
// A finalized or failed writer throws std::logic_error before those argument
// checks. A 32-bit integer is never shifted by 32.
//
// Only one partial output byte is buffered. Filled bits occupy its high
// positions and the unused low bits stay zero. A full byte is emitted
// immediately. There is no intermediate bit container.
//
// finish() emits a partial byte padded with zero bits on the right and
// returns how many padding bits were added, which is 0 through 7. A later
// finish() returns that same count and writes nothing. The destructor does
// not emit the partial byte. Writing after finish() throws std::logic_error.
//
// If the output iterator throws, that exception propagates and the writer
// refuses further writes and finalization. Bytes the sink already accepted
// stay where they are. The failed byte is not retried, and a partially
// written sink is not rolled back.
//
// Moving a writer transfers the partial byte and the output iterator. A
// successful move assignment discards the destination writer's previous
// pending bits without writing them. The moved-from writer rejects further
// writes and finalization. Moving invalidates output iterators that referred
// to either writer.
//
// If the output iterator's move constructor or move assignment throws, every
// writer involved in that move is left unusable. Pending bits are discarded
// and are not emitted, including when the iterator changes its sink and then
// throws.
//
// output_iterator() is a C++20 output iterator of bool. Copies share this
// writer's packing state.
template<class Out>
  requires std::output_iterator<Out, std::byte>
class bit_writer// NOLINT(readability-identifier-naming)
{
public:
  explicit bit_writer(Out output) : output_(std::move(output)) {}

  bit_writer(bit_writer const &) = delete;
  bit_writer &operator=(bit_writer const &) = delete;

  bit_writer(bit_writer &&other) noexcept
    requires std::is_nothrow_move_constructible_v<Out>
    : output_(std::move(other.output_))
  { TransferFrom(other); }

  // A throwing output-iterator move must not leave a usable writer. The new
  // writer is not constructed, and the source is retired because its iterator
  // may already have changed sinks.
  bit_writer(bit_writer &&other)
    requires(!std::is_nothrow_move_constructible_v<Out>)
  try : output_(std::move(other.output_)) {
    TransferFrom(other);
  } catch (...) {
    other.Retire();
    throw;
  }

  bit_writer &operator=(bit_writer &&other) noexcept
    requires std::is_nothrow_move_assignable_v<Out>
  {
    if (this != &other) {
      output_ = std::move(other.output_);
      TransferFrom(other);
    }
    return *this;
  }

  // Move assignment replaces this writer's output first. If that assignment
  // throws after retargeting the sink, both writers are retired so the
  // destination's old pending bits cannot be emitted to the new sink.
  bit_writer &operator=(bit_writer &&other)
    requires(!std::is_nothrow_move_assignable_v<Out>)
  {
    if (this != &other) {
      try {
        output_ = std::move(other.output_);
      } catch (...) {
        Retire();
        other.Retire();
        throw;
      }
      TransferFrom(other);
    }
    return *this;
  }

  ~bit_writer() = default;

  void write_bit(bool bit)// NOLINT(readability-identifier-naming)
  { write_bits(bit ? std::uint32_t{ 1 } : std::uint32_t{ 0 }, 1U); }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters,readability-identifier-naming)
  void write_bits(std::uint32_t value, unsigned bit_count)
  {
    EnsureWritable();
    detail::CodewordStatus const status = detail::CheckCodeword(value, bit_count);
    if (status == detail::CodewordStatus::kCountTooLarge) {
      throw std::invalid_argument("dacoexec::bit_writer bit count must be at most 32");
    }
    if (status == detail::CodewordStatus::kValueTooWide) {
      throw std::invalid_argument("dacoexec::bit_writer value has bits set outside the codeword width");
    }
    if (bit_count == 0U) { return; }

    unsigned remaining = bit_count;
    while (remaining > 0U) {
      unsigned const space = detail::kBitsPerByte - pending_bits_;
      unsigned const chunk = remaining < space ? remaining : space;
      unsigned const shift = remaining - chunk;
      std::uint32_t const extracted = (value >> shift) & detail::LowBitMask(chunk);
      AcceptChunk(extracted, chunk);
      remaining -= chunk;
    }
  }

  // Emits the final partial byte, if any, and returns the zero-padding count.
  [[nodiscard]] std::uint8_t finish()
  {
    if (failed_) { throw std::logic_error("dacoexec::bit_writer is unusable"); }
    if (finalized_) { return padding_; }

    std::uint8_t const padding =
      pending_bits_ == 0U ? std::uint8_t{ 0 } : static_cast<std::uint8_t>(detail::kBitsPerByte - pending_bits_);
    if (pending_bits_ != 0U) { EmitByte(); }
    padding_ = padding;
    finalized_ = true;
    return padding_;
  }

  [[nodiscard]] bit_output_iterator<Out> output_iterator() noexcept// NOLINT(readability-identifier-naming)
  { return bit_output_iterator<Out>{ *this }; }

private:
  void EnsureWritable() const
  {
    if (failed_) { throw std::logic_error("dacoexec::bit_writer is unusable"); }
    if (finalized_) { throw std::logic_error("dacoexec::bit_writer write after finalization"); }
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void AcceptChunk(std::uint32_t extracted, unsigned chunk)
  {
    unsigned const space = detail::kBitsPerByte - pending_bits_;
    unsigned const insert_shift = space - chunk;
    std::uint32_t const placed = extracted << insert_shift;
    accumulator_ = static_cast<std::uint8_t>(static_cast<std::uint32_t>(accumulator_) | placed);
    pending_bits_ += chunk;
    if (pending_bits_ == detail::kBitsPerByte) { EmitByte(); }
  }

  void EmitByte()
  {
    auto const value = static_cast<std::byte>(accumulator_);
    try {
      *output_ = value;
      ++output_;
    } catch (...) {
      failed_ = true;
      throw;
    }
    accumulator_ = 0;
    pending_bits_ = 0;
  }

  void TransferFrom(bit_writer &other) noexcept
  {
    accumulator_ = other.accumulator_;
    pending_bits_ = other.pending_bits_;
    padding_ = other.padding_;
    finalized_ = other.finalized_;
    failed_ = other.failed_;
    other.Retire();
  }

  void Retire() noexcept
  {
    accumulator_ = 0;
    pending_bits_ = 0;
    padding_ = 0;
    finalized_ = true;
    failed_ = true;
  }

  Out output_;
  std::uint8_t accumulator_ = 0;
  unsigned pending_bits_ = 0;
  std::uint8_t padding_ = 0;
  bool finalized_ = false;
  bool failed_ = false;
};

template<class Out>
  requires std::output_iterator<Out, std::byte>
// NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
auto bit_output_iterator<Out>::Proxy::operator=(bool bit) const -> Proxy const &
{
  writer_->write_bit(bit);
  return *this;
}

}// namespace dacoexec
