//! \file
//! Typed node ownership with stable addresses for constant and runtime evaluation.

#ifndef DACOEXEC_MEMORY_NODE_STORAGE_HPP
#define DACOEXEC_MEMORY_NODE_STORAGE_HPP

#include <array>
#include <cstddef>
#include <dacoexec/error.hpp>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dacoexec::detail::memory {

/**
 * Fixed-capacity object pool. All slots are default-constructed up front.
 * Copy and move are disabled because nodes can contain pointers into this pool.
 * Created objects live until the pool is destroyed; individual release is unsupported.
 */
template<class T, std::size_t capacity> class static_node_arena
{
public:
  using value_type = T;

  constexpr static_node_arena() = default;
  static_node_arena(static_node_arena const &) = delete;
  static_node_arena(static_node_arena &&) = delete;
  auto operator=(static_node_arena const &) -> static_node_arena & = delete;
  auto operator=(static_node_arena &&) -> static_node_arena & = delete;
  constexpr ~static_node_arena() = default;

  [[nodiscard]] constexpr auto create(T value) -> T *
  {
    if (used_ == capacity) { throw ::dacoexec::exception(::dacoexec::errc::capacity_exceeded, "Node arena exhausted"); }
    auto *node = &storage_.at(used_);
    *node = std::move(value);
    ++used_;
    return node;
  }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return used_; }

private:
  std::array<T, capacity> storage_{};
  std::size_t used_ = 0;
};

/**
 * Owns separately allocated typed nodes with stable addresses. The default
 * allocator supports C++20 constant evaluation when the owner is destroyed in
 * that evaluation. Custom allocators may impose runtime-only restrictions.
 * The allocator must use T* pointers. Node bookkeeping uses std::vector.
 */
template<class T, class Allocator = std::allocator<T>> class dynamic_node_allocator
{
  using allocator_traits = std::allocator_traits<Allocator>;
  static_assert(std::is_same_v<typename allocator_traits::value_type, T>);
  static_assert(std::is_same_v<typename allocator_traits::pointer, T *>);
  static constexpr std::size_t nodes_per_allocation = 1;

public:
  using value_type = T;

  constexpr dynamic_node_allocator() = default;

  constexpr explicit dynamic_node_allocator(Allocator const &allocator) : allocator_(allocator) {}

  dynamic_node_allocator(dynamic_node_allocator const &) = delete;
  dynamic_node_allocator(dynamic_node_allocator &&) = delete;
  auto operator=(dynamic_node_allocator const &) -> dynamic_node_allocator & = delete;
  auto operator=(dynamic_node_allocator &&) -> dynamic_node_allocator & = delete;

  constexpr ~dynamic_node_allocator()
  {
    for (auto *node : nodes_) {
      allocator_traits::destroy(allocator_, node);
      allocator_traits::deallocate(allocator_, node, nodes_per_allocation);
    }
  }

  [[nodiscard]] constexpr auto create(T value) -> T *
  {
    auto *node = allocator_traits::allocate(allocator_, nodes_per_allocation);
    try {
      allocator_traits::construct(allocator_, node, std::move(value));
    } catch (...) {
      allocator_traits::deallocate(allocator_, node, nodes_per_allocation);
      throw;
    }
    try {
      nodes_.push_back(node);
    } catch (...) {
      allocator_traits::destroy(allocator_, node);
      allocator_traits::deallocate(allocator_, node, nodes_per_allocation);
      throw;
    }
    return node;
  }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return nodes_.size(); }

private:
  Allocator allocator_{};
  std::vector<T *> nodes_{};
};

}// namespace dacoexec::detail::memory

#endif// DACOEXEC_MEMORY_NODE_STORAGE_HPP
