//! \file
//! Supported MSB-first bit input, output, and boolean output iterators.
#ifndef DACOEXEC_BIT_HPP
#define DACOEXEC_BIT_HPP
#include <cstddef>
#include <cstdint>
#include <dacoexec/detail/bit/codeword.hpp>
#include <dacoexec/detail/support/byte.hpp>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dacoexec {


/**
 * Low bits of a value and their width for MSB-first packing.
 * Widths above 32 and values with set bits outside the width are rejected by bit_writer.
 */
struct bit_sequence
{
  std::uint32_t value_ = 0;
  unsigned bit_count_ = 0;
};


template<class Out>
  requires std::output_iterator<Out, std::byte>
class bit_writer;

/**
 * Output iterator that writes each assigned bool as one bit of a bit_writer.
 *
 * Copying the iterator copies the handle, not the packing state. Every copy
 * refers to the same writer. Incrementing does not emit a byte. The iterator
 * must not outlive the writer, and a move of the writer invalidates it.
 */
template<class Out>
  requires std::output_iterator<Out, std::byte>
class bit_output_iterator
{
public:
  using iterator_category = std::output_iterator_tag;
  using iterator_concept = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  /**
   * Assignment handle forwarding a bool to the owning writer.
   *
   * It shares the iterator lifetime requirements; assignment may throw as write_bit().
   */
  class proxy
  {
  public:
    /**
     * Appends one bit through the bound writer, with write_bit()'s failure behavior.
     */
    // Const assignment is required by std::indirectly_writable.
    // NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
    auto operator=(bool bit) const -> proxy const &;

  private:
    friend class bit_output_iterator;

    explicit proxy(bit_writer<Out> *writer) noexcept : writer_(writer) {}

    bit_writer<Out> *writer_ = nullptr;
  };

  /**
   * Binds a non-owning handle; writer must remain alive and unmoved.
   */
  explicit bit_output_iterator(bit_writer<Out> &writer) noexcept : writer_(&writer) {}

  [[nodiscard]] auto operator*() const noexcept -> proxy { return proxy{ writer_ }; }

  auto operator++() noexcept -> bit_output_iterator & { return *this; }

  auto operator++([[maybe_unused]] int postfix) noexcept -> bit_output_iterator { return *this; }

private:
  bit_writer<Out> *writer_ = nullptr;
};

/**
 * Packs bits into bytes, most significant bit first.
 *
 * write_bit writes one bit. write_bits writes the low `bit_count` bits of
 * `value`, highest of those bits first. `bit_count` is 0 through 32. A
 * zero-length write of a zero value does nothing. Counts above 32, and values
 * with bits set outside the declared width, throw std::invalid_argument.
 * A finalized or failed writer throws std::logic_error before those argument
 * checks. A 32-bit integer is never shifted by 32.
 *
 * Only one partial output byte is buffered. Filled bits occupy its high
 * positions and the unused low bits stay zero. A full byte is emitted
 * immediately. There is no intermediate bit container.
 *
 * finish() emits a partial byte padded with zero bits on the right and
 * returns how many padding bits were added, which is 0 through 7. A later
 * finish() returns that same count and writes nothing. The destructor does
 * not emit the partial byte. Writing after finish() throws std::logic_error.
 *
 * If the output iterator throws, that exception propagates and the writer
 * refuses further writes and finalization. Bytes the sink already accepted
 * stay where they are. The failed byte is not retried, and a partially
 * written sink is not rolled back.
 *
 * Moving a writer transfers the partial byte and the output iterator. A
 * successful move assignment discards the destination writer's previous
 * pending bits without writing them. The moved-from writer rejects further
 * writes and finalization. Moving invalidates output iterators that referred
 * to either writer.
 *
 * If the output iterator's move constructor or move assignment throws, every
 * writer involved in that move is left unusable. Pending bits are discarded
 * and are not emitted, including when the iterator changes its sink and then
 * throws.
 *
 * output_iterator() is a C++20 output iterator of bool. Copies share this
 * writer's packing state.
 */
template<class Out>
  requires std::output_iterator<Out, std::byte>
class bit_writer
{
public:
  /**
   * Takes ownership of the output cursor and starts with no pending bits.
   *
   * Moving the cursor into the writer may throw; destination storage is not owned.
   */
  explicit bit_writer(Out output) : output_(std::move(output)) {}

  bit_writer(bit_writer const &) = delete;
  auto operator=(bit_writer const &) -> bit_writer & = delete;

  /**
   * Transfers the cursor and pending bits, preserving finalization and failure state.
   *
   * The source becomes unusable. Existing handles to the source are invalidated.
   * If a cursor move throws, the source is retired and no writer is constructed.
   * @see bit_writer
   */
  bit_writer(bit_writer &&other) noexcept
    requires std::is_nothrow_move_constructible_v<Out>
    : output_(std::move(other.output_))
  { transfer_from(other); }

  // A throwing output-iterator move must not leave a usable writer. The new
  // writer is not constructed, and the source is retired because its iterator
  // may already have changed sinks.
  // Throwing iterator moves are part of the contract.
  bit_writer(bit_writer &&other) noexcept(false)
    requires(!std::is_nothrow_move_constructible_v<Out>)
  try : output_(std::move(other.output_)) {
    transfer_from(other);
  } catch (...) {

    other.retire();
    throw;
  }

  /**
   * Replaces the cursor and pending state, discarding this writer's pending bits.
   *
   * Self-assignment is a no-op. Otherwise both writers' handles are invalidated
   * and the source becomes unusable. If cursor assignment throws, both writers
   * are retired without emitting pending bits.
   * @see bit_writer
   */
  auto operator=(bit_writer &&other) noexcept -> bit_writer &
    requires std::is_nothrow_move_assignable_v<Out>
  {
    if (this != &other) {
      output_ = std::move(other.output_);
      transfer_from(other);
    }
    return *this;
  }

  // Move assignment replaces this writer's output first. If that assignment
  // throws after retargeting the sink, both writers are retired so the
  // destination's old pending bits cannot be emitted to the new sink.
  // Throwing iterator moves are part of the contract.
  auto operator=(bit_writer &&other) noexcept(false) -> bit_writer &
    requires(!std::is_nothrow_move_assignable_v<Out>)
  {
    if (this != &other) {
      try {
        output_ = std::move(other.output_);
      } catch (...) {
        retire();
        other.retire();
        throw;
      }
      transfer_from(other);
    }
    return *this;
  }

  ~bit_writer() = default;

  /**
   * Appends one bit, emitting a byte immediately when it becomes full.
   *
   * @throws std::logic_error If finalized, failed, or moved from.
   * Output-iterator exceptions propagate and make the writer unusable.
   */
  auto write_bit(bool bit) -> void
  { write_bits({ .value_ = bit ? std::uint32_t{ 1 } : std::uint32_t{ 0 }, .bit_count_ = 1U }); }

  /**
   * Writes the low bit_count bits of value, highest bit first.
   *
   * @param bits Value and width from zero through 32; zero width requires value zero.
   * @throws std::invalid_argument If the width or value is invalid.
   * @throws std::logic_error If unusable or finalized, checked before arguments.
   * Output exceptions propagate, retire the writer, and leave accepted bytes intact.
   */
  auto write_bits(bit_sequence bits) -> void
  {
    ensure_writable();
    auto const [value, bit_count] = bits;
    detail::bit::codeword_status const status = detail::bit::check_codeword(bits);
    if (status == detail::bit::codeword_status::count_too_large) {
      throw std::invalid_argument("dacoexec::bit_writer bit count must be at most 32");
    }
    if (status == detail::bit::codeword_status::value_too_wide) {
      throw std::invalid_argument("dacoexec::bit_writer value has bits set outside the codeword width");
    }
    if (bit_count == 0U) { return; }

    unsigned remaining = bit_count;
    while (remaining > 0U) {
      unsigned const space = detail::support::bits_per_byte - pending_bits_;
      unsigned const chunk = remaining < space ? remaining : space;
      unsigned const shift = remaining - chunk;
      std::uint32_t const extracted = (value >> shift) & detail::bit::low_bit_mask(chunk);
      accept_chunk({ .value_ = extracted, .bit_count_ = chunk });
      remaining -= chunk;
    }
  }

  /**
   * Emits the final partial byte, if any, and returns the zero-padding count.
   *
   * @return Padding bits from zero through seven; repeated calls return the same count.
   * @throws std::logic_error If failed or moved from.
   * Output exceptions propagate and permanently disable the writer.
   */
  [[nodiscard]] auto finish() -> std::uint8_t
  {
    if (failed_) { throw std::logic_error("dacoexec::bit_writer is unusable"); }
    if (finalized_) { return padding_; }

    std::uint8_t const padding = pending_bits_ == 0U
                                   ? std::uint8_t{ 0 }
                                   : static_cast<std::uint8_t>(detail::support::bits_per_byte - pending_bits_);
    if (pending_bits_ != 0U) { emit_byte(); }
    padding_ = padding;
    finalized_ = true;
    return padding_;
  }

  /**
   * Returns a non-owning bool output iterator sharing this writer's packing state.
   *
   * @code
   * auto output = writer.output_iterator();
   * *output++ = true;
   * auto padding = writer.finish();
   * @endcode
   *
   * @see bit_output_iterator
   */
  [[nodiscard]] auto output_iterator() noexcept -> bit_output_iterator<Out>
  { return bit_output_iterator<Out>{ *this }; }

private:
  auto ensure_writable() const -> void
  {
    if (failed_) { throw std::logic_error("dacoexec::bit_writer is unusable"); }
    if (finalized_) { throw std::logic_error("dacoexec::bit_writer write after finalization"); }
  }

  auto accept_chunk(bit_sequence bits) -> void
  {
    auto const [extracted, chunk] = bits;
    unsigned const space = detail::support::bits_per_byte - pending_bits_;
    unsigned const insert_shift = space - chunk;
    std::uint32_t const placed = extracted << insert_shift;
    accumulator_ = static_cast<std::uint8_t>(static_cast<std::uint32_t>(accumulator_) | placed);
    pending_bits_ += chunk;
    if (pending_bits_ == detail::support::bits_per_byte) { emit_byte(); }
  }

  auto emit_byte() -> void
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

  auto transfer_from(bit_writer &other) noexcept -> void
  {
    accumulator_ = other.accumulator_;
    pending_bits_ = other.pending_bits_;
    padding_ = other.padding_;
    finalized_ = other.finalized_;
    failed_ = other.failed_;
    other.retire();
  }

  auto retire() noexcept -> void
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

// Const assignment satisfies the output iterator proxy contract.
// NOLINTBEGIN(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
template<class Out>
  requires std::output_iterator<Out, std::byte>
auto bit_output_iterator<Out>::proxy::operator=(bool bit) const -> proxy const &
{
  writer_->write_bit(bit);
  return *this;
}

// NOLINTEND(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)


/**
 * Reads only the declared valid bits of a byte span. Storage must outlive the reader.
 * Invalid widths throw invalid_argument; exhausted reads throw out_of_range.
 * Failed read_bits calls leave the position unchanged. Padding is never consumed;
 * validating padding bytes belongs to the containing format.
 */
class bit_reader
{
public:
  constexpr explicit bit_reader(std::span<std::byte const> bytes, std::size_t valid_bits)
    : bytes_(bytes), remaining_(valid_bits)
  {
    auto const whole_bytes = valid_bits / detail::support::bits_per_byte;
    auto const partial_bits = valid_bits % detail::support::bits_per_byte;
    if (whole_bytes > bytes.size() || (whole_bytes == bytes.size() && partial_bits != 0U)) {
      throw std::invalid_argument("dacoexec::bit_reader bit limit exceeds storage");
    }
  }

  [[nodiscard]] constexpr auto remaining_bits() const noexcept -> std::size_t { return remaining_; }

  constexpr auto read_bit() -> bool
  {
    if (remaining_ == 0U) { throw std::out_of_range("dacoexec::bit_reader exhausted input"); }
    auto const shift =
      detail::support::bits_per_byte - 1U - static_cast<unsigned>(position_ % detail::support::bits_per_byte);
    auto const value = std::to_integer<unsigned>(bytes_.subspan(position_ / detail::support::bits_per_byte).front());
    ++position_;
    --remaining_;
    return ((value >> shift) & 1U) != 0U;
  }

  constexpr auto read_bits(unsigned count) -> std::uint64_t
  {
    if (count > std::numeric_limits<std::uint64_t>::digits) {
      throw std::invalid_argument("dacoexec::bit_reader width exceeds uint64_t");
    }
    if (count > remaining_) { throw std::out_of_range("dacoexec::bit_reader exhausted input"); }
    std::uint64_t value = 0;
    for (unsigned index = 0; index < count; ++index) { value = (value << 1U) | (read_bit() ? 1U : 0U); }
    return value;
  }

private:
  std::span<std::byte const> bytes_;
  std::size_t remaining_ = 0;
  std::size_t position_ = 0;
};


}// namespace dacoexec
#endif// DACOEXEC_BIT_HPP
