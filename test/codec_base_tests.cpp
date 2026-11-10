#include "error_test_utils.hpp"
#include <dacoexec/detail/ans/decoder.hpp>
#include <dacoexec/detail/ans/encoded_block.hpp>
#include <dacoexec/detail/ans/encoder.hpp>
#include <dacoexec/detail/codec/decoder_base.hpp>
#include <dacoexec/detail/codec/encoder_base.hpp>
#include <dacoexec/detail/huffman/decoder.hpp>
#include <dacoexec/detail/huffman/encoder.hpp>
#include <dacoexec/detail/views/blocks.hpp>
#include <dacoexec/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {
constexpr std::uint64_t sequence = 42;
constexpr std::size_t input_size = 4096;
constexpr std::size_t candidate_size = 16;
constexpr std::size_t metadata_cost = 8;
constexpr auto symbol = std::byte{ 'A' };

// Controllable costs exercise ties and overflow without allocating huge blocks.
struct candidate_encoder
  : dacoexec::detail::codec::encoder_base<candidate_encoder, dacoexec::detail::ans::encoded_block>
{
  candidate_encoder() = default;

  std::size_t metadata_ = metadata_cost;

  [[nodiscard]] static auto try_encode(std::span<std::byte const> /*input*/)
    -> std::optional<dacoexec::detail::ans::encoded_block>
  {
    dacoexec::detail::ans::encoded_block block;
    block.encoding_ = dacoexec::detail::ans::block_encoding::rans;
    block.payload_.resize(candidate_size);
    return block;
  }

  [[nodiscard]] auto metadata_size(dacoexec::detail::ans::encoded_block const & /*block*/) const -> std::size_t
  { return metadata_; }
};

struct short_decoder : dacoexec::detail::codec::decoder_base<short_decoder, dacoexec::detail::ans::encoded_block>
{
  short_decoder() = default;

  [[nodiscard]] static auto output_limit_message() -> char const * { return "Output limit"; }

  [[nodiscard]] static auto raw_size_message() -> char const * { return "RAW size"; }

  [[nodiscard]] static auto is_raw(dacoexec::detail::ans::encoded_block const & /*block*/) -> bool { return false; }

  static auto validate(dacoexec::detail::ans::encoded_block const & /*block*/) -> void {}

  [[nodiscard]] static auto decode_compressed(dacoexec::detail::ans::encoded_block const & /*block*/,
    std::size_t /*limit*/) -> std::vector<std::byte>
  { return {}; }
};

template<typename Encoder, typename Decoder, typename Error> auto check_codec() -> void
{
  auto encoded = Encoder{}.encode({ sequence, std::vector<std::byte>(input_size, symbol) });
  CHECK(encoded.sequence_ == sequence);
  CHECK(encoded.uncompressed_size_ == input_size);
  CHECK(encoded.payload_.size() < input_size);
  auto decoded = Decoder{}.decode(std::move(encoded), input_size);
  CHECK(decoded.sequence_ == sequence);
  CHECK(decoded.bytes_ == std::vector<std::byte>(input_size, symbol));

  dacoexec::detail::byte_block input{ sequence, { symbol } };
  auto const *storage = input.bytes_.data();
  auto raw = Encoder{}.encode(std::move(input));
  CHECK(raw.payload_.data() == storage);
  auto output = Decoder{}.decode(std::move(raw), 1);
  CHECK(output.bytes_.data() == storage);
  CHECK(output.sequence_ == sequence);

  auto oversized = Encoder{}.encode({ sequence, { symbol } });
  CHECK_THROWS_MATCHES(Decoder{}.decode(std::move(oversized), 0),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::output_limit_exceeded));
  auto inconsistent = Encoder{}.encode({ sequence, { symbol } });
  inconsistent.uncompressed_size_ = 0;
  CHECK_THROWS_MATCHES(Decoder{}.decode(std::move(inconsistent), 1),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));

  auto empty = Decoder{}.decode(Encoder{}.encode({ sequence, {} }), 0);
  CHECK(empty.sequence_ == sequence);
  CHECK(empty.bytes_.empty());
}
}// namespace

TEST_CASE("Shared encoder rejects equal and overflowing total costs", "[codec]")
{
  candidate_encoder encoder;
  constexpr auto tie_size = candidate_size + metadata_cost;
  auto tie = encoder.encode({ sequence, std::vector<std::byte>(tie_size, symbol) });
  CHECK(tie.encoding_ == dacoexec::detail::ans::block_encoding::raw);
  auto smaller = encoder.encode({ sequence, std::vector<std::byte>(tie_size + 1U, symbol) });
  CHECK(smaller.encoding_ == dacoexec::detail::ans::block_encoding::rans);
  CHECK(smaller.sequence_ == sequence);
  CHECK(smaller.uncompressed_size_ == tie_size + 1U);
  encoder.metadata_ = std::numeric_limits<std::size_t>::max();
  auto overflow = encoder.encode({ sequence, std::vector<std::byte>(input_size, symbol) });
  CHECK(overflow.encoding_ == dacoexec::detail::ans::block_encoding::raw);
}

TEST_CASE("Shared decoder rejects incomplete codec output", "[codec]")
{
  dacoexec::detail::ans::encoded_block block;
  block.uncompressed_size_ = input_size;
  CHECK_THROWS_MATCHES(short_decoder{}.decode(std::move(block), input_size),
    dacoexec::exception,
    dacoexec::test::error_is(dacoexec::errc::invalid_payload));
}

TEST_CASE("Codec classes preserve ownership, sequences, and bounds", "[codec]")
{
  check_codec<dacoexec::detail::huffman::encoder, dacoexec::detail::huffman::decoder, dacoexec::exception>();
  check_codec<dacoexec::detail::ans::encoder, dacoexec::detail::ans::decoder, dacoexec::exception>();
}
