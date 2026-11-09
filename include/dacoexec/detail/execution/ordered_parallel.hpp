//! \file
//! Optional bounded parallel processing with sequential source callbacks.

#ifndef DACOEXEC_EXECUTION_ORDERED_PARALLEL_HPP
#define DACOEXEC_EXECUTION_ORDERED_PARALLEL_HPP

#include <dacoexec/detail/execution/ordered_sender.hpp>

namespace dacoexec::detail::execution {
/**
 * Default maximum number of inputs staged in a parallel batch.
 */
inline constexpr std::size_t default_max_in_flight = 4;

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
  std::size_t max_in_flight = default_max_in_flight)
  -> ::dacoexec::detail::execution::ordered_sender<Scheduler, Source, Process>
{ return { std::move(scheduler), std::move(source), std::move(process), max_in_flight }; }
}// namespace dacoexec::detail::execution
#endif// DACOEXEC_EXECUTION_ORDERED_PARALLEL_HPP
