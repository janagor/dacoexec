#include <dacoexec/huffman/frequency.hpp>

#include <dacoexec/huffman/tree.hpp>

#include <dacoexec/bit/bit_output_iterator.hpp>

#include <dacoexec/views/bits.hpp>

#include <dacoexec/views/buffered_bytes.hpp>

#include <dacoexec/dacoexec.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 202002L);
#else
static_assert(__cplusplus >= 202002L);
#endif

static_assert(std::ranges::input_range<std::span<std::byte const>>);

constexpr auto signed_high = std::bit_cast<signed char>(static_cast<unsigned char>(0xB2));
constexpr auto char_high = std::bit_cast<char>(static_cast<unsigned char>(0x80));

static_assert(dacoexec::detail::to_byte(std::byte{ 0xB2 }) == std::byte{ 0xB2 });
static_assert(dacoexec::detail::to_byte(static_cast<unsigned char>(0x80)) == std::byte{ 0x80 });
static_assert(dacoexec::detail::to_byte(signed_high) == std::byte{ 0xB2 });
static_assert(dacoexec::detail::to_byte(char_high) == std::byte{ 0x80 });
static_assert(dacoexec::detail::to_byte(static_cast<signed char>(-1)) == std::byte{ 0xFF });
static_assert(dacoexec::detail::to_byte(static_cast<char>(-1)) == std::byte{ 0xFF });
static_assert(dacoexec::detail::to_byte(char8_t{ 0xFF }) == std::byte{ 0xFF });

static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 0U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 1U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 2U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 3U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 4U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 5U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 6U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0xB2 }, 7U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0x80 }, 0U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0x80 }, 7U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0x01 }, 0U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0x01 }, 7U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0x00 }, 3U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0xFF }, 3U));

static_assert(dacoexec::detail::low_bit_mask(0U) == 0U);
static_assert(dacoexec::detail::low_bit_mask(1U) == 1U);
static_assert(dacoexec::detail::low_bit_mask(8U) == 0xFFU);
static_assert(dacoexec::detail::low_bit_mask(31U) == 0x7FFFFFFFU);
static_assert(dacoexec::detail::low_bit_mask(32U) == 0xFFFFFFFFU);

static_assert(dacoexec::detail::check_codeword(0U, 0U) == dacoexec::detail::codeword_status::ok);
static_assert(dacoexec::detail::check_codeword(1U, 0U) == dacoexec::detail::codeword_status::value_too_wide);
static_assert(dacoexec::detail::check_codeword(0U, 33U) == dacoexec::detail::codeword_status::count_too_large);
static_assert(dacoexec::detail::check_codeword(0xFFFFFFFFU, 32U) == dacoexec::detail::codeword_status::ok);
static_assert(dacoexec::detail::check_codeword(0x1FFU, 8U) == dacoexec::detail::codeword_status::value_too_wide);
static_assert(dacoexec::detail::check_codeword(0xFFU, 8U) == dacoexec::detail::codeword_status::ok);
static_assert(dacoexec::detail::check_codeword(1U, 32U) == dacoexec::detail::codeword_status::ok);

using bit_range = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::bits());

static_assert(std::ranges::input_range<bit_range>);
static_assert(!std::ranges::forward_range<bit_range>);
static_assert(!std::ranges::bidirectional_range<bit_range>);
static_assert(!std::ranges::random_access_range<bit_range>);
static_assert(!std::ranges::sized_range<bit_range>);
static_assert(std::ranges::view<bit_range>);
static_assert(!std::ranges::borrowed_range<bit_range>);
static_assert(std::same_as<std::ranges::range_value_t<bit_range>, bool>);
static_assert(std::same_as<std::ranges::range_reference_t<bit_range>, bool>);
static_assert(std::input_iterator<std::ranges::iterator_t<bit_range>>);
static_assert(!std::copyable<bit_range>);
static_assert(std::movable<bit_range>);
static_assert(std::is_nothrow_move_constructible_v<bit_range>);

template<class R>
concept can_adapt_bits = requires(R &range) { range | dacoexec::views::bits(); };

static_assert(can_adapt_bits<std::vector<std::byte>>);
static_assert(can_adapt_bits<std::span<std::byte const>>);
static_assert(can_adapt_bits<std::vector<unsigned char>>);
static_assert(can_adapt_bits<std::vector<char>>);
static_assert(can_adapt_bits<std::vector<signed char>>);
static_assert(can_adapt_bits<std::vector<char8_t>>);
static_assert(!can_adapt_bits<std::vector<int>>);
static_assert(!can_adapt_bits<std::vector<std::uint16_t>>);
static_assert(!can_adapt_bits<std::vector<bool>>);

using buffered_bytes = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::buffered_bytes(8));

static_assert(std::ranges::input_range<buffered_bytes>);
static_assert(!std::ranges::forward_range<buffered_bytes>);
static_assert(!std::ranges::sized_range<buffered_bytes>);
static_assert(std::ranges::view<buffered_bytes>);
static_assert(!std::ranges::borrowed_range<buffered_bytes>);
static_assert(dacoexec::byte_input_range<buffered_bytes>);
static_assert(std::same_as<std::ranges::range_value_t<buffered_bytes>, std::byte>);
static_assert(std::same_as<std::ranges::range_reference_t<buffered_bytes>, std::byte>);
static_assert(requires(buffered_bytes &&view) { std::move(view) | dacoexec::views::bits(); });

using byte_out = std::back_insert_iterator<std::vector<std::byte>>;
using bit_writer = dacoexec::bit_writer<byte_out>;
using bit_out = decltype(std::declval<bit_writer &>().output_iterator());

static_assert(std::output_iterator<bit_out, bool>);
static_assert(std::indirectly_writable<bit_out, bool>);
static_assert(std::copyable<bit_out>);
static_assert(!std::copyable<bit_writer>);
static_assert(std::movable<bit_writer>);
static_assert(std::same_as<bit_out, dacoexec::bit_output_iterator<byte_out>>);

template<class Out>
concept can_make_writer = requires(Out output) { dacoexec::bit_writer<Out>{ std::move(output) }; };

static_assert(can_make_writer<byte_out>);
static_assert(!can_make_writer<std::vector<std::byte>::const_iterator>);
static_assert(!can_make_writer<int>);

static_assert(dacoexec::huffman::alphabet_size == 256);
static_assert(std::is_same_v<dacoexec::huffman::frequency_table, std::array<std::uint64_t, 256>>);
static_assert(std::is_same_v<dacoexec::huffman::code_length_table, std::array<std::uint8_t, 256>>);

constexpr std::string_view huffman_sample = "AAAAABBCD";
constexpr auto huffman_frequencies = dacoexec::huffman::count_frequencies(huffman_sample);
static_assert(huffman_frequencies.at(static_cast<unsigned char>('A')) == 5);
static_assert(huffman_frequencies.at(static_cast<unsigned char>('B')) == 2);
static_assert(huffman_frequencies.at(static_cast<unsigned char>('C')) == 1);
static_assert(huffman_frequencies.at(static_cast<unsigned char>('D')) == 1);
static_assert(huffman_frequencies.at(0) == 0);

constexpr auto huffman_lengths = dacoexec::huffman::build_code_lengths(huffman_frequencies);
static_assert(huffman_lengths.at(static_cast<unsigned char>('A')) == 1);
static_assert(huffman_lengths.at(static_cast<unsigned char>('B')) == 2);
static_assert(huffman_lengths.at(static_cast<unsigned char>('C')) == 3);
static_assert(huffman_lengths.at(static_cast<unsigned char>('D')) == 3);
static_assert(huffman_lengths.at(0) == 0);

static_assert(dacoexec::huffman::count_frequencies(std::string_view{}) == dacoexec::huffman::frequency_table{});
static_assert(dacoexec::huffman::build_code_lengths({}) == dacoexec::huffman::code_length_table{});

constexpr auto only_a_lengths =
  dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(std::string_view{ "A" }));

constexpr bool single_symbol_length(dacoexec::huffman::code_length_table const &lengths, unsigned char symbol)
{
  if (lengths.at(symbol) != 1) { return false; }
  for (std::size_t index = 0; index < dacoexec::huffman::alphabet_size; ++index) {
    if (index == symbol) { continue; }
    if (lengths.at(index) != 0) { return false; }
  }
  return true;
}

static_assert(single_symbol_length(only_a_lengths, static_cast<unsigned char>('A')));

constexpr std::array<char, 2> huffman_pair{ { 'X', 'Y' } };
constexpr auto pair_lengths = dacoexec::huffman::build_code_lengths(dacoexec::huffman::count_frequencies(huffman_pair));
static_assert(pair_lengths.at(static_cast<unsigned char>('X')) == 1);
static_assert(pair_lengths.at(static_cast<unsigned char>('Y')) == 1);
static_assert(pair_lengths.at(static_cast<unsigned char>('Z')) == 0);

constexpr std::array<unsigned char, 3> huffman_bytes{ { 0x00, 0x80, 0xFF } };
constexpr auto byte_frequencies = dacoexec::huffman::count_frequencies(huffman_bytes);
static_assert(byte_frequencies.at(0x00) == 1);
static_assert(byte_frequencies.at(0x80) == 1);
static_assert(byte_frequencies.at(0xFF) == 1);
static_assert(byte_frequencies.at(1) == 0);

constexpr auto signed_all_ones = std::bit_cast<signed char>(static_cast<unsigned char>(0xFF));
constexpr std::array<signed char, 1> signed_bytes{ { signed_all_ones } };
constexpr auto signed_frequencies = dacoexec::huffman::count_frequencies(signed_bytes);
static_assert(signed_frequencies.at(0xFF) == 1);
static_assert(signed_frequencies.at(0) == 0);

constexpr auto accumulated_frequencies = [] {
  dacoexec::huffman::frequency_table table{};
  dacoexec::huffman::accumulate_frequencies(table, std::string_view{ "AA" });
  dacoexec::huffman::accumulate_frequencies(table, std::string_view{ "ABB" });
  return table;
}();
static_assert(accumulated_frequencies.at(static_cast<unsigned char>('A')) == 3);
static_assert(accumulated_frequencies.at(static_cast<unsigned char>('B')) == 2);
static_assert(accumulated_frequencies.at(0) == 0);

constexpr auto canonical_empty = dacoexec::huffman::make_canonical_codebook({});
static_assert(canonical_empty.lengths() == dacoexec::huffman::code_length_table{});
static_assert(canonical_empty[0] == dacoexec::huffman::codeword{});
constexpr auto canonical_single = dacoexec::huffman::make_canonical_codebook(only_a_lengths);
static_assert(canonical_single['A'] == dacoexec::huffman::codeword{ 0, 1 });
constexpr auto canonical_sample = dacoexec::huffman::make_canonical_codebook(huffman_lengths);
static_assert(canonical_sample['A'] == dacoexec::huffman::codeword{ 0b0, 1 });
static_assert(canonical_sample['B'] == dacoexec::huffman::codeword{ 0b10, 2 });
static_assert(canonical_sample['C'] == dacoexec::huffman::codeword{ 0b110, 3 });
static_assert(canonical_sample['D'] == dacoexec::huffman::codeword{ 0b111, 3 });

static_assert([]() -> bool {
  using namespace dacoexec::huffman;
  code_length_table lengths{};
  constexpr std::uint8_t byte_width = 8;
  lengths.fill(byte_width);
  auto const book = make_canonical_codebook(lengths);
  for (std::size_t symbol = 0; symbol < alphabet_size; ++symbol) {
    if (book[static_cast<std::uint8_t>(symbol)] != codeword{ static_cast<std::uint32_t>(symbol), byte_width }) {
      return false;
    }
  }
  return true;
}());

static_assert([]() -> bool {
  using namespace dacoexec::huffman;
  code_length_table lengths{};
  for (unsigned length = 1; length < max_canonical_bits; ++length) {
    lengths.at(length - 1U) = static_cast<std::uint8_t>(length);
  }
  lengths.at(max_canonical_bits - 1U) = max_canonical_bits;
  lengths.at(max_canonical_bits) = max_canonical_bits;
  auto const book = make_canonical_codebook(lengths);
  return book[max_canonical_bits - 1U] == codeword{ 0xFFFFFFFEU, max_canonical_bits }
    && book[max_canonical_bits] == codeword{ 0xFFFFFFFFU, max_canonical_bits }
    && try_make_canonical_codebook(lengths).has_value();
}());

int main() { return 0; }
