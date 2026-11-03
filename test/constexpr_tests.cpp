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

constexpr auto kSignedHigh = std::bit_cast<signed char>(static_cast<unsigned char>(0xB2));
constexpr auto kCharHigh = std::bit_cast<char>(static_cast<unsigned char>(0x80));

static_assert(dacoexec::detail::ToByte(std::byte{ 0xB2 }) == std::byte{ 0xB2 });
static_assert(dacoexec::detail::ToByte(static_cast<unsigned char>(0x80)) == std::byte{ 0x80 });
static_assert(dacoexec::detail::ToByte(kSignedHigh) == std::byte{ 0xB2 });
static_assert(dacoexec::detail::ToByte(kCharHigh) == std::byte{ 0x80 });
static_assert(dacoexec::detail::ToByte(static_cast<signed char>(-1)) == std::byte{ 0xFF });
static_assert(dacoexec::detail::ToByte(static_cast<char>(-1)) == std::byte{ 0xFF });
static_assert(dacoexec::detail::ToByte(char8_t{ 0xFF }) == std::byte{ 0xFF });

static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 0U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 1U));
static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 2U));
static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 3U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 4U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 5U));
static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 6U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xB2 }, 7U));
static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0x80 }, 0U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0x80 }, 7U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0x01 }, 0U));
static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0x01 }, 7U));
static_assert(!dacoexec::detail::MsbFirstBit(std::uint8_t{ 0x00 }, 3U));
static_assert(dacoexec::detail::MsbFirstBit(std::uint8_t{ 0xFF }, 3U));

static_assert(dacoexec::detail::LowBitMask(0U) == 0U);
static_assert(dacoexec::detail::LowBitMask(1U) == 1U);
static_assert(dacoexec::detail::LowBitMask(8U) == 0xFFU);
static_assert(dacoexec::detail::LowBitMask(31U) == 0x7FFFFFFFU);
static_assert(dacoexec::detail::LowBitMask(32U) == 0xFFFFFFFFU);

static_assert(dacoexec::detail::CheckCodeword(0U, 0U) == dacoexec::detail::CodewordStatus::kOk);
static_assert(dacoexec::detail::CheckCodeword(1U, 0U) == dacoexec::detail::CodewordStatus::kValueTooWide);
static_assert(dacoexec::detail::CheckCodeword(0U, 33U) == dacoexec::detail::CodewordStatus::kCountTooLarge);
static_assert(dacoexec::detail::CheckCodeword(0xFFFFFFFFU, 32U) == dacoexec::detail::CodewordStatus::kOk);
static_assert(dacoexec::detail::CheckCodeword(0x1FFU, 8U) == dacoexec::detail::CodewordStatus::kValueTooWide);
static_assert(dacoexec::detail::CheckCodeword(0xFFU, 8U) == dacoexec::detail::CodewordStatus::kOk);
static_assert(dacoexec::detail::CheckCodeword(1U, 32U) == dacoexec::detail::CodewordStatus::kOk);

using BitRange = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::bits());

static_assert(std::ranges::input_range<BitRange>);
static_assert(!std::ranges::forward_range<BitRange>);
static_assert(!std::ranges::bidirectional_range<BitRange>);
static_assert(!std::ranges::random_access_range<BitRange>);
static_assert(!std::ranges::sized_range<BitRange>);
static_assert(std::ranges::view<BitRange>);
static_assert(!std::ranges::borrowed_range<BitRange>);
static_assert(std::same_as<std::ranges::range_value_t<BitRange>, bool>);
static_assert(std::same_as<std::ranges::range_reference_t<BitRange>, bool>);
static_assert(std::input_iterator<std::ranges::iterator_t<BitRange>>);
static_assert(!std::copyable<BitRange>);
static_assert(std::movable<BitRange>);
static_assert(std::is_nothrow_move_constructible_v<BitRange>);

template<class R>
concept CanAdaptBits = requires(R &range) { range | dacoexec::views::bits(); };

static_assert(CanAdaptBits<std::vector<std::byte>>);
static_assert(CanAdaptBits<std::span<std::byte const>>);
static_assert(CanAdaptBits<std::vector<unsigned char>>);
static_assert(CanAdaptBits<std::vector<char>>);
static_assert(CanAdaptBits<std::vector<signed char>>);
static_assert(CanAdaptBits<std::vector<char8_t>>);
static_assert(!CanAdaptBits<std::vector<int>>);
static_assert(!CanAdaptBits<std::vector<std::uint16_t>>);
static_assert(!CanAdaptBits<std::vector<bool>>);

using BufferedBytes = decltype(std::declval<std::vector<std::byte> &>() | dacoexec::views::buffered_bytes(8));

static_assert(std::ranges::input_range<BufferedBytes>);
static_assert(!std::ranges::forward_range<BufferedBytes>);
static_assert(!std::ranges::sized_range<BufferedBytes>);
static_assert(std::ranges::view<BufferedBytes>);
static_assert(!std::ranges::borrowed_range<BufferedBytes>);
static_assert(dacoexec::byte_input_range<BufferedBytes>);
static_assert(std::same_as<std::ranges::range_value_t<BufferedBytes>, std::byte>);
static_assert(std::same_as<std::ranges::range_reference_t<BufferedBytes>, std::byte>);
static_assert(requires(BufferedBytes &&view) { std::move(view) | dacoexec::views::bits(); });

using ByteOut = std::back_insert_iterator<std::vector<std::byte>>;
using BitWriter = dacoexec::bit_writer<ByteOut>;
using BitOut = decltype(std::declval<BitWriter &>().output_iterator());

static_assert(std::output_iterator<BitOut, bool>);
static_assert(std::indirectly_writable<BitOut, bool>);
static_assert(std::copyable<BitOut>);
static_assert(!std::copyable<BitWriter>);
static_assert(std::movable<BitWriter>);
static_assert(std::same_as<BitOut, dacoexec::bit_output_iterator<ByteOut>>);

template<class Out>
concept CanMakeWriter = requires(Out output) { dacoexec::bit_writer<Out>{ std::move(output) }; };

static_assert(CanMakeWriter<ByteOut>);
static_assert(!CanMakeWriter<std::vector<std::byte>::const_iterator>);
static_assert(!CanMakeWriter<int>);

int main() { return 0; }
