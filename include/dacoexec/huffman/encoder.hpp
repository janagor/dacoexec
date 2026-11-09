//! \file
//! Huffman encoder with early RAW rejection.
#ifndef DACOEXEC_HUFFMAN_ENCODER_HPP
#define DACOEXEC_HUFFMAN_ENCODER_HPP

#include <dacoexec/bit/bit_output_iterator.hpp>
#include <dacoexec/codec/encoder_base.hpp>
#include <dacoexec/huffman/codewords_view.hpp>
#include <dacoexec/huffman/encoded_block.hpp>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>

namespace dacoexec::huffman {

namespace detail {
  // Unsupported widths and unrepresentable bit totals select RAW before
  // allocating an encoded buffer. Lengths come from build_code_lengths;
  // canonical validation and assignment happen once, only if Huffman wins.
  [[nodiscard]] inline auto encoded_bit_count(frequency_table const &frequencies, code_length_table const &lengths)
    -> std::optional<std::uint64_t>
  {
    std::uint64_t total = 0;
    for (auto const symbol : std::views::iota(std::size_t{ 0 }, alphabet_size)) {
      auto const frequency = frequencies.at(symbol);
      auto const length = lengths.at(symbol);
      if (length > max_canonical_bits) { return std::nullopt; }
      if (length == 0) { continue; }

      auto const remaining_bits = std::numeric_limits<std::uint64_t>::max() - total;
      if (frequency > remaining_bits / length) { return std::nullopt; }
      total += frequency * length;
    }
    return total;
  }

}// namespace detail

class encoder : public codec::encoder_base<encoder, encoded_block>
{
public:
  encoder() = default;

  [[nodiscard]] static auto try_encode(std::span<std::byte const> input) -> std::optional<encoded_block>
  {
    auto const frequencies = count_frequencies(input);
    auto const lengths = build_code_lengths(frequencies);
    auto const bits = detail::encoded_bit_count(frequencies, lengths);
    if (bits && *bits != 0) {
      constexpr auto byte_bits = dacoexec::detail::bits_per_byte;
      auto const packed_size = (*bits / byte_bits) + (*bits % byte_bits != 0 ? 1U : 0U);
      // Reject before building the codebook or allocating compressed output.
      if (input.size() > alphabet_size && packed_size < input.size() - alphabet_size) {
        encoded_block result;
        auto const book = make_canonical_codebook(lengths);
        result.payload_.reserve(static_cast<std::size_t>(packed_size));
        dacoexec::bit_writer writer{ std::back_inserter(result.payload_) };
        for (auto const word : input | views::codewords(book)) {
          writer.write_bits({ .value_ = word.bits_, .bit_count_ = word.length_ });
        }
        result.padding_bits_ = writer.finish();
        result.lengths_ = lengths;
        result.encoding_ = block_encoding::canonical_huffman;
        return result;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] static auto metadata_size(encoded_block const & /*block*/) noexcept -> std::size_t { return alphabet_size; }
};

}// namespace dacoexec::huffman
#endif// DACOEXEC_HUFFMAN_ENCODER_HPP
