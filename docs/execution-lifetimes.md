# Ordered execution lifetime review

This review uses NVIDIA/stdexec revision
`ead186b1d8db3ebe37a946ff84a6ce08bf795153`.
The implementation intentionally allows terminal completion to destroy the
currently completing batch operation and its parent operation.

## Pinned completion paths

The upstream files below are relative to that revision's source tree:

- `include/exec/static_thread_pool.hpp`: `_static_thread_pool::run()` removes
  the task from its queue and calls `task->execute_()`. It does not touch that
  task after the call. `_opstate::do_execute_()` retrieves the receiver's stop
  token before dispatching value or stopped completion. Each branch ends with
  the terminal receiver call; it does not access `op` afterward.
- `include/exec/static_thread_pool.hpp`: the parallel `bulk` customization
  distributes a batch through `bulk_enqueue()`, including when submission comes
  from a pool worker. `_bulk_shared_state::bulk_task::do_execute_()` runs its
  share of the indices, increments the completed-agent count, and the last
  agent tail-calls the batch receiver. It does not access shared state after
  terminal completion. The local processing function is `noexcept`, recording
  errors in per-index slots rather than throwing through the bulk algorithm.
- `include/stdexec/__detail/__bulk.hpp`: the generic implementation runs the
  indices before forwarding terminal completion; it does not access the batch
  state after that callback. Inline schedulers use this path.
- `include/stdexec/__detail/__basic_sender.hpp`: `__rcvr::set_value()`,
  `set_error()`, and `set_stopped()` delegate to the sender's completion
  implementation and perform no subsequent state access.
- `include/stdexec/__detail/__then.hpp`: `__then_impl::__complete_fn` delegates
  value completion to `__set_value_from()` and forwards error/stopped directly.
  Neither path reads the operation state after the receiver completes.
- `include/stdexec/__detail/__receivers.hpp`: `__set_value_from()` finishes
  invoking the processing function before calling downstream `set_value()`.
  Processing exceptions become downstream `set_error()`. No sender-state
  access follows either terminal call. A result temporary can be destroyed
  afterward, so result destructors must not depend on the destroyed operation.
- `include/stdexec/__detail/__inline_scheduler.hpp`: the inline operation's
  `start()` ends with `set_value()` and does not access its state afterward.

Upstream source: [pinned stdexec tree](https://github.com/NVIDIA/stdexec/tree/ead186b1d8db3ebe37a946ff84a6ce08bf795153).

## Local invariants

Each batch is submitted as `bulk(schedule(scheduler), par, count, process)`.
Submitting separate schedule operations from the last completing pool worker
can leave small batches on its private queue: the pinned pool's default steal
block contains eight tasks, while the default batch contains four. Bulk
submission distributes those tasks across the pool instead.

Each index owns a distinct result slot. The bulk sender joins its agents before
completing, so slot writes are visible before ordered commit. A dispatch
sentinel prevents inline completion from destroying the batch operation before
`start()` returns. The batch receiver tail-calls `settled()` and never uses its
receiver or parent afterward. The parent releases the bulk operation, input
buffers, and results before acquiring the next batch.

`driving_` permits only one coordinator to advance at a time. Inline child
completion queues another iteration rather than entering the next batch
recursively. The terminal path returns immediately after delivering completion;
the caller may destroy the entire parent from within that completion.

Custom schedulers must likewise permit their operation state to be destroyed
inside a terminal receiver invocation. The executor forwards the original
receiver environment to the batch operation, including its cancellation token.

Internal failure skips indices that have not begun processing and drains
running siblings without requesting sibling cancellation. Scheduling or
connecting a batch can fail before any of its indices run; earlier batches
remain committed. The lowest acquired index with a recorded error wins. A recorded error takes precedence over external stop;
when no error was recorded, external stop or a stopped child produces stopped
completion. A cancellation that prevents a worker from running cannot produce
that worker's hypothetical error.

## Regression coverage

`test/execution_tests.cpp` includes concurrent short-operation stress, inline
and asynchronous destructive receivers, queued stopped children, and mixed
stop/failure races. New synchronization gates have deadlines so a missed worker
can be reported without an indefinitely blocked gate. Existing latch-based
cases retain the CTest timeout as their final guard.

The execution target participates in the compiler matrix, ASan/UBSan job, and
TSan job. The concurrency regression gates every full batch, including batches
submitted by a completing worker, until all four indices are running. The gates
have deadlines so a regression fails instead of waiting indefinitely. A short
final batch also verifies ordered completion. Submission-failure coverage checks
that a failed later batch retains earlier commits.

## Static-analysis scope

Clang 22 reports `core.StackAddressEscape` in stdexec's operation-state return
machinery when the ordered pipeline is instantiated. These immovable results
are constructed directly in their destination through guaranteed copy elision;
the call-site suppression attribute does not suppress diagnostics originating
inside stdexec. The CMake helper `dacoexec_suppress_stdexec_stack_escape` disables
only this checker on the execution-test and benchmark executables. It does not
disable the checker for other test targets, nor any other analysis or runtime
instrumentation. Lifetime and race validation still require the destructive
receiver tests and separate ASan/UBSan and TSan runs.

The test and benchmark directories inherit the root clang-tidy configuration,
with cognitive-complexity checks disabled locally so complete validation
scenarios and measured workloads can remain together.

## PR 9 production changes

No runtime execution defect has been demonstrated during this source-only work.
The `commit_batch()` extraction is retained specifically because the supplied
validation log rejected `advance()` at cognitive complexity 45 (limit 25). It
separates ordered commit/error policy from dispatch without changing the batch
or wire-format contract. Existing reversed-completion, failed-batch, cancellation
and retained-result tests cover those boundaries.

Completion preparation is kept separate from delivery because cppcheck reported
`accessMoved` on the error callback following the moved value receiver. Only
`source_.finish()` and preparation of its result belong inside that exception
handler; terminal callbacks may destroy the operation and must never be
followed by another completion attempt. The focused test “Finish failure delivers
one error to a destructive receiver” exercises this catch path, verifies one
terminal completion, and checks that destruction is safe. It remains unexecuted
until an authorized C++ build is available.

## Bulk dispatch validation (2026-11-12)

After switching batch submission to parallel `bulk`, all 43 execution tests
(2,728 assertions) passed in the Clang 22 Debug, ASan/UBSan, and TSan builds.
The execution public header also compiled on its own. The sanitizer runs include
the destructive-receiver, cancellation, lowest-index failure, and repeated-batch
concurrency tests described above. ASan/UBSan ran outside the sandbox because
LeakSanitizer cannot inspect threads under its tracing restrictions.
