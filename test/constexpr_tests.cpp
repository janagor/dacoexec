#include "huffman_test_utils.hpp"
#include <concepts>
#include <dacoexec/concepts.hpp>
#include <dacoexec/detail/byte.hpp>
#include <dacoexec/huffman/block.hpp>
#include <dacoexec/huffman/canonical.hpp>
#include <dacoexec/huffman/decode_view.hpp>
#include <utility>

#include <dacoexec/huffman/frequency.hpp>

#include <dacoexec/huffman/tree.hpp>

#include <dacoexec/bit/bit_output_iterator.hpp>

#include <dacoexec/views/bits.hpp>

#include <dacoexec/views/buffered_bytes.hpp>

#include <dacoexec/dacoexec.hpp>// IWYU pragma: keep -- Exercise the public umbrella header.

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

namespace {

constexpr auto cxx20_standard = 202002L;
constexpr auto sample_byte = 0xB2;
constexpr auto high_bit_value = 0x80;
constexpr auto all_bits_value = 0xFF;
constexpr auto bit_index_five = 5U;
constexpr auto bit_index_six = 6U;
constexpr auto last_bit_index = dacoexec::detail::bits_per_byte - 1U;
constexpr auto expected_byte_bits = 8U;
constexpr auto byte_mask = 0xFFU;
constexpr auto short_word_width = 31U;
constexpr auto short_word_mask = 0x7FFFFFFFU;
constexpr auto word_width = 32U;
constexpr auto word_mask = 0xFFFFFFFFU;
constexpr auto unsupported_word_width = word_width + 1U;
constexpr auto overwide_byte = 0x1FFU;
constexpr auto buffer_window_size = 8;
constexpr auto expected_alphabet_size = 256;
constexpr auto sample_a_count = 5;
constexpr auto sample_c_code = 0b110;
constexpr auto sample_d_code = 0b111;
constexpr auto penultimate_word = 0xFFFFFFFEU;

}// namespace

#ifdef _MSVC_LANG
static_assert(_MSVC_LANG >= cxx20_standard);
#else
static_assert(__cplusplus >= cxx20_standard);
#endif

static_assert(std::ranges::input_range<std::span<std::byte const>>);

constexpr auto signed_high = std::bit_cast<signed char>(static_cast<unsigned char>(0xB2));
constexpr auto char_high = std::bit_cast<char>(static_cast<unsigned char>(0x80));

static_assert(dacoexec::detail::to_byte(std::byte{ sample_byte }) == std::byte{ sample_byte });
static_assert(dacoexec::detail::to_byte(static_cast<unsigned char>(high_bit_value)) == std::byte{ high_bit_value });
static_assert(dacoexec::detail::to_byte(signed_high) == std::byte{ sample_byte });
static_assert(dacoexec::detail::to_byte(char_high) == std::byte{ high_bit_value });
static_assert(dacoexec::detail::to_byte(static_cast<signed char>(-1)) == std::byte{ all_bits_value });
static_assert(dacoexec::detail::to_byte(static_cast<char>(-1)) == std::byte{ all_bits_value });
static_assert(dacoexec::detail::to_byte(char8_t{ all_bits_value }) == std::byte{ all_bits_value });

static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, 0U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, 1U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, 2U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, 3U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, 4U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, bit_index_five));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, bit_index_six));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ sample_byte }, last_bit_index));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ high_bit_value }, 0U));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ high_bit_value }, last_bit_index));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0x01 }, 0U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ 0x01 }, last_bit_index));
static_assert(!dacoexec::detail::msb_first_bit(std::uint8_t{ 0x00 }, 3U));
static_assert(dacoexec::detail::msb_first_bit(std::uint8_t{ all_bits_value }, 3U));

static_assert(dacoexec::detail::low_bit_mask(0U) == 0U);
static_assert(dacoexec::detail::low_bit_mask(1U) == 1U);
static_assert(dacoexec::detail::low_bit_mask(expected_byte_bits) == byte_mask);
static_assert(dacoexec::detail::low_bit_mask(short_word_width) == short_word_mask);
static_assert(dacoexec::detail::low_bit_mask(word_width) == word_mask);

static_assert(dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = 0U, .bit_count_ = 0U })
              == dacoexec::detail::codeword_status::ok);
static_assert(dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = 1U, .bit_count_ = 0U })
              == dacoexec::detail::codeword_status::value_too_wide);
static_assert(
  dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = 0U, .bit_count_ = unsupported_word_width })
  == dacoexec::detail::codeword_status::count_too_large);
static_assert(dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = word_mask, .bit_count_ = word_width })
              == dacoexec::detail::codeword_status::ok);
static_assert(
  dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = overwide_byte, .bit_count_ = expected_byte_bits })
  == dacoexec::detail::codeword_status::value_too_wide);
static_assert(
  dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = byte_mask, .bit_count_ = expected_byte_bits })
  == dacoexec::detail::codeword_status::ok);
static_assert(dacoexec::detail::check_codeword(dacoexec::bit_sequence{ .value_ = 1U, .bit_count_ = word_width })
              == dacoexec::detail::codeword_status::ok);

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

using buffered_bytes =
  decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::buffered_bytes(buffer_window_size));

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

static_assert(dacoexec::huffman::alphabet_size == expected_alphabet_size);
static_assert(std::is_same_v<dacoexec::huffman::frequency_table, std::array<std::uint64_t, expected_alphabet_size>>);
static_assert(std::is_same_v<dacoexec::huffman::code_length_table, std::array<std::uint8_t, expected_alphabet_size>>);

constexpr std::string_view huffman_sample = "AAAAABBCD";
constexpr auto huffman_frequencies = dacoexec::huffman::count_frequencies(huffman_sample);
static_assert(huffman_frequencies.at(static_cast<unsigned char>('A')) == sample_a_count);
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

constexpr auto single_symbol_length(dacoexec::huffman::code_length_table const &lengths, unsigned char symbol) -> bool
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
static_assert(byte_frequencies.at(high_bit_value) == 1);
static_assert(byte_frequencies.at(all_bits_value) == 1);
static_assert(byte_frequencies.at(1) == 0);

constexpr auto signed_all_ones = std::bit_cast<signed char>(static_cast<unsigned char>(0xFF));
constexpr std::array<signed char, 1> signed_bytes{ { signed_all_ones } };
constexpr auto signed_frequencies = dacoexec::huffman::count_frequencies(signed_bytes);
static_assert(signed_frequencies.at(all_bits_value) == 1);
static_assert(signed_frequencies.at(0) == 0);

constexpr auto accumulated_frequencies = []() -> dacoexec::huffman::frequency_table {
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
static_assert(test_detail::canonical_word(canonical_empty, 0) == dacoexec::huffman::codeword{});
constexpr auto canonical_single = dacoexec::huffman::make_canonical_codebook(only_a_lengths);
static_assert(
  test_detail::canonical_word(canonical_single, 'A') == dacoexec::huffman::codeword{ .bits_ = 0, .length_ = 1 });
constexpr auto canonical_sample = dacoexec::huffman::make_canonical_codebook(huffman_lengths);
static_assert(
  test_detail::canonical_word(canonical_sample, 'A') == dacoexec::huffman::codeword{ .bits_ = 0b0, .length_ = 1 });
static_assert(
  test_detail::canonical_word(canonical_sample, 'B') == dacoexec::huffman::codeword{ .bits_ = 0b10, .length_ = 2 });
static_assert(test_detail::canonical_word(canonical_sample, 'C')
              == dacoexec::huffman::codeword{ .bits_ = sample_c_code, .length_ = 3 });
static_assert(test_detail::canonical_word(canonical_sample, 'D')
              == dacoexec::huffman::codeword{ .bits_ = sample_d_code, .length_ = 3 });

static_assert([]() -> bool {
  dacoexec::huffman::code_length_table lengths{};
  constexpr std::uint8_t byte_width = 8;
  lengths.fill(byte_width);
  auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
  for (std::size_t symbol = 0; symbol < dacoexec::huffman::alphabet_size; ++symbol) {
    if (test_detail::canonical_word(book, static_cast<std::uint8_t>(symbol))
        != dacoexec::huffman::codeword{ .bits_ = static_cast<std::uint32_t>(symbol), .length_ = byte_width }) {
      return false;
    }
  }
  return true;
}());

static_assert([]() -> bool {
  dacoexec::huffman::code_length_table lengths{};
  for (unsigned length = 1; length < dacoexec::huffman::max_canonical_bits; ++length) {
    lengths.at(length - 1U) = static_cast<std::uint8_t>(length);
  }
  lengths.at(dacoexec::huffman::max_canonical_bits - 1U) = dacoexec::huffman::max_canonical_bits;
  lengths.at(dacoexec::huffman::max_canonical_bits) = dacoexec::huffman::max_canonical_bits;
  auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
  return test_detail::canonical_word(book, dacoexec::huffman::max_canonical_bits - 1U)
           == dacoexec::huffman::codeword{ .bits_ = penultimate_word, .length_ = dacoexec::huffman::max_canonical_bits }
         && test_detail::canonical_word(book, dacoexec::huffman::max_canonical_bits)
              == dacoexec::huffman::codeword{ .bits_ = word_mask, .length_ = dacoexec::huffman::max_canonical_bits }
         && dacoexec::huffman::try_make_canonical_codebook(lengths).has_value();
}());

using decode_range = dacoexec::huffman::decode_view<std::ranges::empty_view<std::byte>>;
static_assert(std::ranges::input_range<decode_range>);
static_assert(std::ranges::view<decode_range>);
static_assert(!std::ranges::forward_range<decode_range>);
static_assert(!std::ranges::borrowed_range<decode_range>);
static_assert(!std::copyable<decode_range>);
static_assert(std::movable<decode_range>);
static_assert(std::same_as<std::ranges::range_value_t<decode_range>, std::byte>);
static_assert(!std::copyable<dacoexec::huffman::encoded_block>);
static_assert(std::is_nothrow_move_constructible_v<dacoexec::huffman::encoded_block>);
static_assert(std::same_as<decltype(dacoexec::huffman::compress_block(std::declval<dacoexec::byte_block>())),
  dacoexec::huffman::encoded_block>);

auto main() -> int { return 0; }
