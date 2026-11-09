//! \file
//! Fixed-capacity, constexpr priority queue; slots are default constructed.
#ifndef DACOEXEC_ADT_STATIC_PRIORITY_QUEUE_HPP
#define DACOEXEC_ADT_STATIC_PRIORITY_QUEUE_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace dacoexec::detail::adt {
// Compare follows std::priority_queue: std::less<T> puts the maximum first.
// T must be default constructible, assignable, and swappable.
template<class T, std::size_t Capacity, class Compare = std::less<T>> class static_priority_queue
{
public:
  constexpr static_priority_queue() = default;

  constexpr explicit static_priority_queue(Compare compare) : compare_(std::move(compare)) {}

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return size_; }

  [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }

  constexpr auto push(T value) -> void
  {
    if (size_ == Capacity) { throw std::length_error("Priority queue capacity exceeded"); }
    entries_.at(size_) = std::move(value);
    ++size_;
    std::push_heap(entries_.begin(), end(), compare_);
  }

  [[nodiscard]] constexpr auto pop() -> T
  {
    if (empty()) { throw std::out_of_range("Priority queue is empty"); }
    std::pop_heap(entries_.begin(), end(), compare_);
    --size_;
    return std::move(entries_.at(size_));
  }

private:
  [[nodiscard]] constexpr auto end() -> typename std::array<T, Capacity>::iterator
  { return std::next(entries_.begin(), static_cast<std::ptrdiff_t>(size_)); }

  std::array<T, Capacity> entries_{};
  std::size_t size_ = 0;
  [[no_unique_address]] Compare compare_{};
};
}// namespace dacoexec::detail::adt
#endif// DACOEXEC_ADT_STATIC_PRIORITY_QUEUE_HPP
