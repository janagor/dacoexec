//! Shared single-pass byte cursor over bounded producer chunks.
#ifndef DACOEXEC_VIEWS_ARCHIVE_BYTES_HPP
#define DACOEXEC_VIEWS_ARCHIVE_BYTES_HPP
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <utility>

namespace dacoexec::detail::views {
// Producer::next() returns storage valid until its next call. An empty chunk is EOF.
template<class Producer> class archive_bytes_view : public std::ranges::view_interface<archive_bytes_view<Producer>>
{
  struct state
  {
    explicit state(Producer producer) : producer_(std::move(producer)) {}

    auto load() -> void
    {
      try {
        chunk_ = producer_.next();
        index_ = 0;
        finished_ = chunk_.empty();
      } catch (...) {
        chunk_ = {};
        finished_ = true;
        throw;
      }
    }

    auto advance() -> void
    {
      ensure_current();
      ++index_;
    }

    // The initial empty chunk and an increment past a chunk's final byte both
    // leave producer work pending. Only an operation that needs the current
    // position loads it; begin() and counted consumers taking zero do no work.
    auto ensure_current() -> void
    {
      if (!finished_ && index_ == chunk_.size()) { load(); }
    }

    Producer producer_;
    std::span<std::byte const> chunk_;
    std::size_t index_ = 0;
    bool finished_ = false;
  };

public:
  archive_bytes_view() = default;

  explicit archive_bytes_view(Producer producer) : state_(std::make_unique<state>(std::move(producer))) {}

  archive_bytes_view(archive_bytes_view const &) = delete;
  auto operator=(archive_bytes_view const &) -> archive_bytes_view & = delete;
  archive_bytes_view(archive_bytes_view &&) noexcept = default;
  auto operator=(archive_bytes_view &&) noexcept -> archive_bytes_view & = default;
  ~archive_bytes_view() = default;

  class iterator
  {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = std::byte;
    using difference_type = std::ptrdiff_t;
    using reference = std::byte;
    using pointer = void;

    iterator() = default;

    [[nodiscard]] auto operator*() const -> std::byte
    {
      state_->ensure_current();
      return state_->chunk_.subspan(state_->index_).front();
    }

    auto operator++() -> iterator &
    {
      state_->advance();
      return *this;
    }

    auto operator++(int) -> void { ++*this; }

    [[nodiscard]] friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel)
      -> bool
    {
      if (cursor.state_ == nullptr) { return true; }
      cursor.state_->ensure_current();
      return cursor.state_->finished_;
    }

  private:
    friend class archive_bytes_view;

    explicit iterator(state *position) : state_(position) {}

    state *state_ = nullptr;
  };

  [[nodiscard]] auto begin() noexcept -> iterator { return iterator{ state_.get() }; }

  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return {}; }

private:
  std::unique_ptr<state> state_;
};
}// namespace dacoexec::detail::views
#endif
