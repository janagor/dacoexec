//! \file
//! Optional bounded parallel processing with sequential source callbacks.

#ifndef DACOEXEC_EXECUTION_ORDERED_PARALLEL_HPP
#define DACOEXEC_EXECUTION_ORDERED_PARALLEL_HPP

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

namespace dacoexec::execution {
/**
 * Default maximum number of inputs staged in a parallel batch.
 */
inline constexpr std::size_t default_max_in_flight = 4;

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

namespace detail {
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

      struct worker_receiver
      {
        using receiver_concept = stdexec::receiver_tag;
        operation *owner_;
        std::size_t index_;

        // Cancellation uses the parent's token unchanged, so preserve its
        // entire environment, including allocator and domain queries.
        [[nodiscard]] auto get_env() const noexcept -> stdexec::env_of_t<Receiver>
        { return stdexec::get_env(owner_->receiver_); }
        template<class Value> auto set_value(Value &&value) noexcept -> void
        {
          auto *owner = owner_;
          try {
            owner->slots_.at(index_).value_.emplace(std::forward<Value>(value));
          } catch (...) {
            owner->slots_.at(index_).error_ = std::current_exception();
          }
          if (owner->slots_.at(index_).error_) { owner->failed_.store(true, std::memory_order_release); }
          owner->settled();
        }
        template<class Error> auto set_error(Error &&error) noexcept -> void
        {
          auto *owner = owner_;
          if constexpr (std::is_same_v<std::remove_cvref_t<Error>, std::exception_ptr>) {
            owner->slots_.at(index_).error_ = std::forward<Error>(error);
          } else {
            try {
              owner->slots_.at(index_).error_ = std::make_exception_ptr(std::forward<Error>(error));
            } catch (...) {
              owner->slots_.at(index_).error_ = std::current_exception();
            }
          }
          owner->failed_.store(true, std::memory_order_release);
          // Drain this batch before reporting the lowest-index error. Do not
          // cancel siblings: a lower-index scheduled job may also fail.
          owner->settled();
        }
        auto set_stopped() noexcept -> void
        {
          auto *owner = owner_;
          owner->slots_.at(index_).stopped_ = true;
          owner->failed_.store(true, std::memory_order_release);
          owner->settled();
        }
      };
      struct work_function
      {
        Process *process_;
        input_type input_;
        auto operator()() -> result_type { return std::invoke(*process_, std::move(input_)); }
      };
      using work_sender =
        decltype(stdexec::then(stdexec::schedule(std::declval<Scheduler &>()), std::declval<work_function>()));
      using child_operation = stdexec::connect_result_t<work_sender, worker_receiver>;

      operation(Scheduler scheduler, Source source, Process process, std::size_t capacity, Receiver receiver)
        : scheduler_(std::move(scheduler)), source_(std::move(source)), process_(std::move(process)),
          capacity_(capacity), receiver_(std::move(receiver)),
          token_(stdexec::get_stop_token(stdexec::get_env(receiver_)))
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
            if (capacity_ == 0) { throw std::invalid_argument("Invalid parallel batch capacity"); }
            source_.initialize();
          }
        } catch (...) {
          error_ = std::current_exception();
        }
        drive();
      }

      // The last completion may destroy its own child and this parent. Each
      // completion path must tail-call settled() and never inspect state after
      // it returns. See docs/execution-lifetimes.md for the pinned sender trace.
      auto settled() noexcept -> void
      {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) { drive(); }
      }

      // A dispatch sentinel prevents an inline completion from inspecting a
      // partially constructed batch. The work counter trampolines synchronous
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
          if (!slot.value_) { throw std::logic_error("Completed worker has no result"); }
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

      auto dispatch_batch(std::vector<input_type> &inputs) -> void
      {
        slots_.resize(inputs.size());
        children_.reserve(inputs.size());
        failed_.store(false, std::memory_order_release);
        remaining_.store(1, std::memory_order_release);// dispatch sentinel
        batch_active_ = true;
        for (std::size_t index = 0; index < inputs.size(); ++index) {
          if (stop_requested() || failed_.load(std::memory_order_acquire)) { break; }
          try {
            auto work =
              stdexec::then(stdexec::schedule(scheduler_), work_function{ &process_, std::move(inputs.at(index)) });
            auto child = std::unique_ptr<child_operation>(
              new child_operation(stdexec::connect(std::move(work), worker_receiver{ this, index })));
            children_.push_back(std::move(child));
            remaining_.fetch_add(1, std::memory_order_relaxed);
            stdexec::start(*children_.back());
          } catch (...) {
            slots_.at(index).error_ = std::current_exception();
            break;
          }
        }
        settled();// release sentinel; a ready batch queues another drive
      }

      auto advance() noexcept -> bool
      {
        try {
          if (batch_active_) {
            if (remaining_.load(std::memory_order_acquire) != 0) { return false; }
            commit_batch();
            children_.clear();
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
          dispatch_batch(inputs);
          return false;
        } catch (...) {
          error_ = std::current_exception();
          return complete();
        }
      }

      auto complete() noexcept -> bool
      {
        // All paths here have either not dispatched or fully drained a batch.
        children_.clear();
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
      std::vector<std::unique_ptr<child_operation>> children_;
      std::atomic<std::size_t> remaining_{ 0 };
      std::atomic<std::size_t> driving_{ 0 };
      std::exception_ptr error_;
      bool batch_active_ = false;
      std::atomic<bool> failed_{ false };
    };

    template<stdexec::receiver Receiver> [[nodiscard]] auto connect(Receiver receiver) && -> operation<Receiver>
    { return { std::move(scheduler_), std::move(source_), std::move(process_), capacity_, std::move(receiver) }; }
  };
}// namespace detail

/**
 * Process must support concurrent invocation on distinct owned inputs. Source
 * callbacks are sequential, but may run on the thread completing the last job.
 *
 * Returns a lazy, single-use sender owning scheduler, source, and process.
 * Source defines input_type/result_type and initialize(), next(), commit(T),
 * finish(); next() yields optional<input_type>, finish() supplies the result.
 * Inputs are acquired in batches and processed concurrently, then committed
 * in acquisition order. Process is shared and must be safe for concurrent calls.
 * A failing batch is drained before reporting its lowest-index worker error;
 * that batch is not committed. Earlier commits are not rolled back. Source or
 * commit exceptions complete with std::exception_ptr. A stop request prevents
 * further acquisition/commits when observed and completes stopped after draining.
 * Errors take precedence over stopped completion; finish() runs only on success.
 *
 * @param max_in_flight Positive batch capacity; zero reports invalid_argument
 *     through the error channel when started, unless already stopped.
 * @return Sender completing with source.finish()'s value, std::exception_ptr,
 *     or stopped. Execution-context lifetimes must extend through completion.
 */
template<stdexec::scheduler Scheduler, class Source, class Process>
[[nodiscard]] auto ordered_parallel_on(Scheduler scheduler,
  Source source,
  Process process,
  std::size_t max_in_flight = default_max_in_flight) -> detail::ordered_sender<Scheduler, Source, Process>
{ return { std::move(scheduler), std::move(source), std::move(process), max_in_flight }; }
}// namespace dacoexec::execution
#endif// DACOEXEC_EXECUTION_ORDERED_PARALLEL_HPP
