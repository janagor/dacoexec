#include <dacoexec/huffman/decode_view.hpp>

static_assert(std::ranges::input_range<dacoexec::huffman::decode_view<std::ranges::empty_view<std::byte>>>);

[[nodiscard]] auto installed_lazy_decode() -> bool
{
  dacoexec::huffman::code_length_table lengths{};
  constexpr auto symbol = static_cast<std::uint8_t>('A');
  lengths.at(symbol) = 1;
  auto const book = dacoexec::huffman::make_canonical_codebook(lengths);
  std::array<std::byte, 1> payload{};
  auto decoded = payload | dacoexec::huffman::views::decode(book, 1, 1);
  auto cursor = decoded.begin();
  if (cursor == decoded.end() || *cursor != std::byte{ 'A' }) { return false; }
  ++cursor;
  return cursor == decoded.end();
}
