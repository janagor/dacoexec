#include "error_test_utils.hpp"
#include <dacoexec/detail/adt/static_priority_queue.hpp>
#include <dacoexec/detail/ans/format.hpp>
#include <dacoexec/detail/huffman/format.hpp>
#include <dacoexec/detail/io/output_sink.hpp>
#include <dacoexec/detail/views/ans_frame_parser.hpp>
#include <dacoexec/detail/views/huffman_frame_parser.hpp>
#include <dacoexec/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
// A stateful ordering verifies that the reusable heap honors its comparator.
struct order
{
  bool minimum_first_ = false;

  constexpr auto operator()(int lhs, int rhs) const -> bool { return minimum_first_ ? lhs > rhs : lhs < rhs; }
};

constexpr auto constexpr_heap_ordering() -> bool
{
  dacoexec::detail::adt::static_priority_queue<int, 3, order> queue{ order{ true } };
  queue.push(3);
  queue.push(1);
  queue.push(2);
  return queue.pop() == 1 && queue.pop() == 2 && queue.pop() == 3 && queue.empty();
}

static_assert(constexpr_heap_ordering());

template<class Format> auto check_parser_sequence_boundary(bool extra_data) -> void
{
  using header_type = decltype(typename Format::frame_type{}.header_);
  using kind_type = decltype(header_type::kind_);
  constexpr auto last_sequence = std::numeric_limits<std::uint64_t>::max();
  std::vector<std::byte> archive;
  dacoexec::detail::io::iterator_sink sink{ std::back_inserter(archive) };
  // RAW framing is shared; no model/code-length metadata is needed here.
  auto const append_raw = [&sink]() -> void {
    sink.write(dacoexec::detail::codec::encode_frame_header(
      header_type{ .kind_ = kind_type::raw, .uncompressed_size_ = 1, .payload_size_ = 1 }));
    sink.write(std::vector<std::byte>{ std::byte{ 'A' } });
  };
  // Header bytes come from the codec, while the lifecycle uses its format policy.
  if constexpr (std::is_same_v<Format, dacoexec::detail::support::huffman_frame_format>) {
    dacoexec::detail::huffman::write_archive_header(sink);
  } else {
    dacoexec::detail::ans::write_archive_header(sink);
  }
  append_raw();
  if (extra_data) { append_raw(); }
  sink.write(dacoexec::detail::codec::encode_frame_header(header_type{}));

  auto source = std::views::all(archive);
  dacoexec::detail::codec::frame_parser<decltype(source), Format> parser{ source, 1 };
  parser.next_sequence_ = last_sequence;
  parser.ensure_initialized();
  REQUIRE(parser.current_.has_value());
  REQUIRE(parser.current_->block_.sequence_ == last_sequence);
  if (extra_data) {
    REQUIRE_THROWS_MATCHES(
      parser.load_next_frame(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::numeric_overflow));
    REQUIRE_FALSE(parser.current_.has_value());
    REQUIRE(parser.finished_);
    // Failure is terminal: later calls must not read or publish another frame.
    REQUIRE_NOTHROW(parser.load_next_frame());
    REQUIRE_FALSE(parser.current_.has_value());
  } else {
    REQUIRE_NOTHROW(parser.load_next_frame());
    REQUIRE_FALSE(parser.current_.has_value());
    REQUIRE(parser.finished_);
  }
}
}// namespace

TEST_CASE("static priority queues retain ordering and contents at capacity", "[adt][codec]")
{
  dacoexec::detail::adt::static_priority_queue<int, 2, order> queue{ order{ false } };
  queue.push(1);
  queue.push(2);
  REQUIRE_THROWS_MATCHES(
    queue.push(3), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::capacity_exceeded));
  REQUIRE(queue.size() == 2);
  REQUIRE(queue.pop() == 2);
  queue.push(3);
  REQUIRE(queue.pop() == 3);
  REQUIRE(queue.pop() == 1);
  REQUIRE_THROWS_MATCHES(queue.pop(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_state));

  dacoexec::detail::adt::static_priority_queue<int, 0> empty;
  REQUIRE_THROWS_MATCHES(
    empty.push(1), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::capacity_exceeded));
  REQUIRE_THROWS_MATCHES(empty.pop(), dacoexec::exception, dacoexec::test::error_is(dacoexec::errc::invalid_state));
  REQUIRE(empty.empty());
}

TEST_CASE("frame parsers accept END at sequence exhaustion and reject further data", "[codec][ranges]")
{
  for (auto const extra_data : { false, true }) {
    check_parser_sequence_boundary<dacoexec::detail::support::huffman_frame_format>(extra_data);
    check_parser_sequence_boundary<dacoexec::detail::support::ans_frame_format>(extra_data);
  }
}
