//! \file
#ifndef DACOEXEC_EXECUTION_DETAIL_ORDERED_PARALLEL_HPP
#define DACOEXEC_EXECUTION_DETAIL_ORDERED_PARALLEL_HPP

#include <dacoexec/error.hpp>
#include <stdexec/execution.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dacoexec::detail::execution {
/**
 * Move-only storage for a worker result, exception, or stopped completion.
 */
template<class T> struct result_slot
{
  //! Value from a successfully completed worker.
  std::optional<T> value_;
  //! Captured worker error, if any.
  std::exception_ptr error_;
  //! Whether the worker completed stopped.
  bool stopped_ = false;

  result_slot() = default;
  result_slot(result_slot &&) = default;
  auto operator=(result_slot &&) -> result_slot & = default;
  result_slot(result_slot const &) = delete;
  auto operator=(result_slot const &) -> result_slot & = delete;
  ~result_slot() = default;
};

// Source supplies input_type, result_type, initialize(), next(), commit(T)
// and finish(). next() returns optional<input_type>; finish() returns the
// final value. Only process is called concurrently (on distinct inputs).
template<class Scheduler, class Source, class Process> struct ordered_sender
{
  using sender_concept = stdexec::sender_tag;
  using value_type = decltype(std::declval<Source &>().finish());
  using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t(value_type),
    stdexec::set_error_t(std::exception_ptr),
    stdexec::set_stopped_t()>;

  Scheduler scheduler_;
  Source source_;
  Process process_;
  std::size_t capacity_;

  template<class Receiver> struct operation
  {
    using operation_state_concept = stdexec::operation_state_tag;
    using input_type = Source::input_type;
    using result_type = Source::result_type;
    using stop_token = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;

    struct batch_receiver
    {
      using receiver_concept = stdexec::receiver_tag;
      operation *owner_;

      // Cancellation uses the parent's token unchanged, so preserve its
      // entire environment, including allocator and domain queries.
      [[nodiscard]] auto get_env() const noexcept -> stdexec::env_of_t<Receiver>
      { return stdexec::get_env(owner_->receiver_); }

      auto set_value() noexcept -> void { owner_->settled(); }

      template<class Error> auto set_error(Error &&error) noexcept -> void
      {
        auto *owner = owner_;
        if constexpr (std::is_same_v<std::remove_cvref_t<Error>, std::exception_ptr>) {
          owner->error_ = std::forward<Error>(error);
        } else {
          try {
            owner->error_ = std::make_exception_ptr(std::forward<Error>(error));
          } catch (...) {
            owner->error_ = std::current_exception();
          }
        }
        owner->settled();
      }

      auto set_stopped() noexcept -> void
      {
        auto *owner = owner_;
        owner->stopped_ = true;
        owner->settled();
      }
    };

    struct work_function
    {
      operation *owner_;

      auto operator()(std::size_t index) const noexcept -> void
      {
        auto &slot = owner_->slots_.at(index);
        if (owner_->stop_requested() || owner_->failed_.load(std::memory_order_acquire)) {
          slot.stopped_ = true;
          return;
        }
        try {
          slot.value_.emplace(std::invoke(owner_->process_, std::move(owner_->inputs_.at(index))));
        } catch (...) {
          slot.error_ = std::current_exception();
          // Drain running siblings without cancelling them: a lower-index
          // block may still fail and must take precedence at ordered commit.
          owner_->failed_.store(true, std::memory_order_release);
        }
      }
    };

    using work_sender = decltype(stdexec::bulk(stdexec::schedule(std::declval<Scheduler &>()),
      stdexec::par,
      std::size_t{},
      std::declval<work_function>()));
    using child_operation = stdexec::connect_result_t<work_sender, batch_receiver>;

    operation(Scheduler scheduler, Source source, Process process, std::size_t capacity, Receiver receiver)
      : scheduler_(std::move(scheduler)), source_(std::move(source)), process_(std::move(process)), capacity_(capacity),
        receiver_(std::move(receiver)), token_(stdexec::get_stop_token(stdexec::get_env(receiver_)))
    {}

    operation(operation const &) = delete;
    operation(operation &&) = delete;
    auto operator=(operation const &) -> operation & = delete;
    auto operator=(operation &&) -> operation & = delete;
    ~operation() = default;

    auto start() noexcept -> void
    {
      try {
        if (!stop_requested()) {
          if (capacity_ == 0) {
            throw ::dacoexec::exception(::dacoexec::errc::invalid_argument, "Invalid parallel batch capacity");
          }
          source_.initialize();
        }
      } catch (...) {
        error_ = std::current_exception();
      }
      drive();
    }

    // Batch completion may destroy its operation and this parent. Each
    // completion path must tail-call settled() and never inspect state after
    // it returns. See docs/execution-lifetimes.md for the pinned sender trace.
    auto settled() noexcept -> void
    {
      if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) { drive(); }
    }

    // A dispatch sentinel keeps inline completion from releasing the batch
    // operation before start() returns. The work counter trampolines synchronous
    // schedulers and serializes all source/commit calls without a mutex.
    auto drive() noexcept -> void
    {
      if (driving_.fetch_add(1, std::memory_order_acq_rel) != 0) { return; }
      while (true) {
        if (advance()) { return; }// completion may destroy *this
        if (driving_.fetch_sub(1, std::memory_order_acq_rel) == 1) { return; }
      }
    }

    auto commit_batch() -> void
    {
      // Slot order is acquisition order. A failed or stopped batch is never committed.
      auto const failed =
        std::ranges::find_if(slots_, [](auto const &slot) -> bool { return static_cast<bool>(slot.error_); });
      if (failed != slots_.end()) {
        error_ = failed->error_;
        return;
      }
      if (error_ || stop_requested()) { return; }
      if (std::ranges::any_of(slots_, [](auto const &slot) -> bool { return slot.stopped_; })) {
        stopped_ = true;
        return;
      }
      for (auto &slot : slots_) {
        if (stop_requested()) { return; }
        if (!slot.value_) {
          throw ::dacoexec::exception(::dacoexec::errc::invalid_state, "Completed worker has no result");
        }
        source_.commit(std::move(*slot.value_));
      }
    }

    auto acquire_inputs() -> std::vector<input_type>
    {
      std::vector<input_type> inputs;
      inputs.reserve(capacity_);
      while (inputs.size() < capacity_ && !stop_requested()) {
        auto input = source_.next();
        if (!input) { break; }
        inputs.push_back(std::move(*input));
      }
      return inputs;
    }

    auto dispatch_batch(std::vector<input_type> inputs) -> void
    {
      inputs_ = std::move(inputs);
      slots_.resize(inputs_.size());
      failed_.store(false, std::memory_order_release);
      // Submit the entire batch so schedulers can distribute it directly.
      // Individual schedule() calls from the completing worker can stay on
      // its private queue, serializing batches smaller than its steal block.
      auto work = stdexec::bulk(stdexec::schedule(scheduler_), stdexec::par, inputs_.size(), work_function{ this });
      batch_.reset(new child_operation(stdexec::connect(std::move(work), batch_receiver{ this })));
      remaining_.store(2, std::memory_order_release);// batch completion plus dispatch sentinel
      batch_active_ = true;
      stdexec::start(*batch_);
      settled();// release sentinel; a ready batch queues another drive
    }

    auto advance() noexcept -> bool
    {
      try {
        if (batch_active_) {
          if (remaining_.load(std::memory_order_acquire) != 0) { return false; }
          commit_batch();
          batch_.reset();
          inputs_.clear();
          slots_.clear();
          batch_active_ = false;
        }
        if (error_ || stop_requested()) { return complete(); }
        auto inputs = acquire_inputs();
        if (stop_requested()) {
          inputs.clear();
          return complete();
        }
        if (inputs.empty()) { return complete(); }
        dispatch_batch(std::move(inputs));
        return false;
      } catch (...) {
        error_ = std::current_exception();
        return complete();
      }
    }

    auto complete() noexcept -> bool
    {
      // All paths here have either not dispatched or fully drained a batch.
      batch_.reset();
      inputs_.clear();
      slots_.clear();
      if (error_) {
        stdexec::set_error(std::move(receiver_), std::move(error_));
      } else if (stop_requested()) {
        stdexec::set_stopped(std::move(receiver_));
      } else {
        auto value = [this]() -> std::optional<value_type> {
          try {
            return source_.finish();
          } catch (...) {
            error_ = std::current_exception();
            return std::nullopt;
          }
        }();
        if (value) {
          stdexec::set_value(std::move(receiver_), std::move(*value));
        } else {
          stdexec::set_error(std::move(receiver_), std::move(error_));
        }
      }
      return true;
    }

    [[nodiscard]] auto stop_requested() const noexcept -> bool { return stopped_ || token_.stop_requested(); }

    Scheduler scheduler_;
    Source source_;
    Process process_;
    std::size_t capacity_;
    Receiver receiver_;
    stop_token token_;
    bool stopped_ = false;
    std::vector<result_slot<result_type>> slots_;
    std::vector<input_type> inputs_;
    std::unique_ptr<child_operation> batch_;
    std::atomic<std::size_t> remaining_{ 0 };
    std::atomic<std::size_t> driving_{ 0 };
    std::exception_ptr error_;
    bool batch_active_ = false;
    std::atomic<bool> failed_{ false };
  };

  template<stdexec::receiver Receiver> [[nodiscard]] auto connect(Receiver receiver) && -> operation<Receiver>
  { return { std::move(scheduler_), std::move(source_), std::move(process_), capacity_, std::move(receiver) }; }
};

}// namespace dacoexec::detail::execution
#endif// DACOEXEC_EXECUTION_DETAIL_ORDERED_PARALLEL_HPP
