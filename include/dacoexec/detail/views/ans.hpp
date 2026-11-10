//! \file
//! Single-pass staged block adaptation. Dereference borrows a stable result;
//! iter_move explicitly transfers ownership. Increment stages the next block.
#ifndef DACOEXEC_VIEWS_ANS_HPP
#define DACOEXEC_VIEWS_ANS_HPP
#include <dacoexec/detail/ans/block.hpp>
#include <dacoexec/error.hpp>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dacoexec::detail::views {
struct rans_block_encoder
{
  model::frequency_model<model::alphabet_size> model_;

  auto operator()(byte_block block) const -> ans::encoded_block
  {
    return { .sequence_ = block.sequence_,
      .encoding_ = ans::block_encoding::rans,
      .uncompressed_size_ = block.bytes_.size(),
      .payload_ = ans::rans::encode_block(block.bytes_, model_),
      .model_ = model_ };
  }
};

struct rans_block_decoder
{
  std::size_t limit_;

  auto operator()(ans::encoded_block block) const -> byte_block
  { return ans::decompress_block(std::move(block), limit_); }
};

template<std::ranges::view V, class Operation>
class ans_block_view : public std::ranges::view_interface<ans_block_view<V, Operation>>
{
  using result_type = std::invoke_result_t<Operation &, std::ranges::range_rvalue_reference_t<V>>;

  struct state
  {
    V base_;
    Operation operation_;
    std::optional<std::ranges::iterator_t<V>> cursor_;
    std::optional<result_type> current_;
    bool initialized_ = false;
    bool finished_ = false;

    state(V base, Operation operation) : base_(std::move(base)), operation_(std::move(operation)) {}

    auto load() -> void
    {
      current_.reset();
      if (!cursor_) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "ANS block cursor is not initialized");
      }
      if (*cursor_ == std::ranges::end(base_)) {
        finished_ = true;
        return;
      }
      current_.emplace(operation_(std::ranges::iter_move(*cursor_)));
    }

    auto initialize() -> void
    {
      if (initialized_) { return; }
      initialized_ = true;
      try {
        cursor_.emplace(std::ranges::begin(base_));
        load();
      } catch (...) {
        finished_ = true;
        current_.reset();
        throw;
      }
    }

    auto advance() -> void
    {
      if (finished_) { return; }
      try {
        if (!cursor_) {
          throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "ANS block cursor is not initialized");
        }
        ++*cursor_;
        load();
      } catch (...) {
        finished_ = true;
        current_.reset();
        throw;
      }
    }
  };

  std::unique_ptr<state> state_;

public:
  ans_block_view() = default;

  explicit ans_block_view(V base, Operation operation)
    : state_(std::make_unique<state>(std::move(base), std::move(operation)))
  {}

  class iterator
  {
    state *state_ = nullptr;
    friend class ans_block_view;

    explicit iterator(state *position) : state_(position) {}

  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = result_type;
    using difference_type = std::ptrdiff_t;

    auto operator*() const -> result_type const &
    {
      if (state_ == nullptr || !state_->current_) {
        throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Iterator has no current value");
      }
      return *state_->current_;
    }

    auto operator++() -> iterator &
    {
      state_->advance();
      return *this;
    }

    auto operator++(int) -> void { ++*this; }

    friend auto operator==(iterator const &cursor, [[maybe_unused]] std::default_sentinel_t sentinel) -> bool
    { return cursor.state_ == nullptr || cursor.state_->finished_; }

    friend auto iter_move(iterator const &cursor) noexcept -> result_type &&
    {
      if (cursor.state_ == nullptr || !cursor.state_->current_) { std::terminate(); }
      return std::move(*cursor.state_->current_);
    }
  };

  auto begin() -> iterator
  {
    if (state_) { state_->initialize(); }
    return iterator{ state_.get() };
  }

  // cppcheck-suppress functionStatic
  [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t { return {}; }
};

template<class Operation> struct ans_closure
{
  Operation operation_;

  template<std::ranges::viewable_range R>
  auto operator()(R &&source) const -> ans_block_view<std::views::all_t<R>, Operation>
  { return ans_block_view<std::views::all_t<R>, Operation>{ std::views::all(std::forward<R>(source)), operation_ }; }

  template<std::ranges::viewable_range R>
  friend auto operator|(R &&source, ans_closure const &closure) -> ans_block_view<std::views::all_t<R>, Operation>
  { return closure(std::forward<R>(source)); }
};
}// namespace dacoexec::detail::views

namespace dacoexec::detail::views::encode {
[[nodiscard]] inline auto rans(model::frequency_model<model::alphabet_size> model)
  -> ::dacoexec::detail::views::ans_closure<::dacoexec::detail::views::rans_block_encoder>
{ return { ::dacoexec::detail::views::rans_block_encoder{ model } }; }
}// namespace dacoexec::detail::views::encode

namespace dacoexec::detail::views::decode {
[[nodiscard]] inline auto rans(std::size_t max_output_bytes)
  -> ::dacoexec::detail::views::ans_closure<::dacoexec::detail::views::rans_block_decoder>
{ return { ::dacoexec::detail::views::rans_block_decoder{ max_output_bytes } }; }
}// namespace dacoexec::detail::views::decode
#endif// DACOEXEC_VIEWS_ANS_HPP
