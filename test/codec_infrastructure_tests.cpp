#include <dacoexec/detail/adt/static_priority_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

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

}// namespace

TEST_CASE("static priority queues retain ordering and contents at capacity", "[adt][codec]")
{
  dacoexec::detail::adt::static_priority_queue<int, 2, order> queue{ order{ false } };
  queue.push(1);
  queue.push(2);
  REQUIRE_THROWS_AS(queue.push(3), std::length_error);
  REQUIRE(queue.size() == 2);
  REQUIRE(queue.pop() == 2);
  queue.push(3);
  REQUIRE(queue.pop() == 3);
  REQUIRE(queue.pop() == 1);
  REQUIRE_THROWS_AS(queue.pop(), std::out_of_range);

  dacoexec::detail::adt::static_priority_queue<int, 0> empty;
  REQUIRE_THROWS_AS(empty.push(1), std::length_error);
  REQUIRE_THROWS_AS(empty.pop(), std::out_of_range);
  REQUIRE(empty.empty());
}
