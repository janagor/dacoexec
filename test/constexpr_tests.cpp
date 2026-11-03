#include <dacoexec/bit/bit_output_iterator.hpp>

#include <dacoexec/views/bits.hpp>

#include <dacoexec/views/buffered_bytes.hpp>

#include <dacoexec/dacoexec.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
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

int main() { return 0; }
